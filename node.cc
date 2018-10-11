// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "tracing/traced_value.h"

#if HAVE_OPENSSL
#include "node_crypto.h"
#endif

#include "ares.h"
#include "nghttp2/nghttp2ver.h"
#include "tracing/node_trace_writer.h"
#include "zlib.h"

#if defined(__POSIX__)
#include <dlfcn.h>
#endif

#include "node_embed.h"

struct node_context_struct {
  v8::Isolate *isolate;
  node::Environment *env;
  node::IsolateData *isolate_data;
  node::ArrayBufferAllocator *allocator;
};

namespace node {

using v8::ArrayBuffer;
using v8::Context;
using v8::EscapableHandleScope;
using v8::HandleScope;
using v8::Isolate;
using v8::Local;
using v8::MaybeLocal;
using v8::Locker;
using v8::ScriptOrigin;
using v8::SealHandleScope;
using v8::String;
using v8::TryCatch;
using v8::V8;
using v8::Value;

static bool v8_is_profiling = false;

// TODO(addaleax): This should not be global.
static bool abort_on_uncaught_exception = false;

// Bit flag used to track security reverts (see node_revert.h)
extern bool v8_initialized;

static Mutex node_isolate_mutex;
static v8::Isolate* node_isolate;

// Ensures that __metadata trace events are only emitted
// when tracing is enabled.
class NodeTraceStateObserver :
    public v8::TracingController::TraceStateObserver {
 public:
  void OnTraceEnabled() override {
    char name_buffer[512];
    if (uv_get_process_title(name_buffer, sizeof(name_buffer)) == 0) {
      // Only emit the metadata event if the title can be retrieved
      // successfully. Ignore it otherwise.
      TRACE_EVENT_METADATA1("__metadata", "process_name",
                            "name", TRACE_STR_COPY(name_buffer));
    }
    TRACE_EVENT_METADATA1("__metadata", "version",
                          "node", NODE_VERSION_STRING);
    TRACE_EVENT_METADATA1("__metadata", "thread_name",
                          "name", "JavaScriptMainThread");

    auto trace_process = tracing::TracedValue::Create();
    trace_process->BeginDictionary("versions");

    const char http_parser_version[] =
        NODE_STRINGIFY(HTTP_PARSER_VERSION_MAJOR)
        "."
        NODE_STRINGIFY(HTTP_PARSER_VERSION_MINOR)
        "."
        NODE_STRINGIFY(HTTP_PARSER_VERSION_PATCH);

    const char node_napi_version[] = NODE_STRINGIFY(NAPI_VERSION);
    const char node_modules_version[] = NODE_STRINGIFY(NODE_MODULE_VERSION);

    trace_process->SetString("http_parser", http_parser_version);
    trace_process->SetString("node", NODE_VERSION_STRING);
    trace_process->SetString("v8", V8::GetVersion());
    trace_process->SetString("uv", uv_version_string());
    trace_process->SetString("zlib", ZLIB_VERSION);
    trace_process->SetString("ares", ARES_VERSION_STR);
    trace_process->SetString("modules", node_modules_version);
    trace_process->SetString("nghttp2", NGHTTP2_VERSION);
    trace_process->SetString("napi", node_napi_version);

#if HAVE_OPENSSL
    // Stupid code to slice out the version string.
    {  // NOLINT(whitespace/braces)
      size_t i, j, k;
      int c;
      for (i = j = 0, k = sizeof(OPENSSL_VERSION_TEXT) - 1; i < k; ++i) {
        c = OPENSSL_VERSION_TEXT[i];
        if ('0' <= c && c <= '9') {
          for (j = i + 1; j < k; ++j) {
            c = OPENSSL_VERSION_TEXT[j];
            if (c == ' ')
              break;
          }
          break;
        }
      }
      trace_process->SetString("openssl",
                              std::string(&OPENSSL_VERSION_TEXT[i], j - i));
    }
#endif
    trace_process->EndDictionary();

    trace_process->SetString("arch", NODE_ARCH);
    trace_process->SetString("platform", NODE_PLATFORM);

    trace_process->BeginDictionary("release");
    trace_process->SetString("name", NODE_RELEASE);
#if NODE_VERSION_IS_LTS
    trace_process->SetString("lts", NODE_VERSION_LTS_CODENAME);
#endif
    trace_process->EndDictionary();
    TRACE_EVENT_METADATA1("__metadata", "node",
                          "process", std::move(trace_process));

    // This only runs the first time tracing is enabled
    controller_->RemoveTraceStateObserver(this);
    delete this;
  }

  void OnTraceDisabled() override {
    // Do nothing here. This should never be called because the
    // observer removes itself when OnTraceEnabled() is called.
    UNREACHABLE();
  }

  explicit NodeTraceStateObserver(v8::TracingController* controller) :
      controller_(controller) {}
  ~NodeTraceStateObserver() override {}

 private:
  v8::TracingController* controller_;
};

struct {
#if NODE_USE_V8_PLATFORM
  void Initialize(int thread_pool_size) {
    tracing_agent_.reset(new tracing::Agent());
    auto controller = tracing_agent_->GetTracingController();
    controller->AddTraceStateObserver(new NodeTraceStateObserver(controller));
    tracing::TraceEventHelper::SetTracingController(controller);
    StartTracingAgent();
    platform_ = new NodePlatform(thread_pool_size, controller);
    V8::InitializePlatform(platform_);
  }

  void Dispose() {
    tracing_agent_.reset(nullptr);
    platform_->Shutdown();
    delete platform_;
    platform_ = nullptr;
  }

  void DrainVMTasks(Isolate* isolate) {
    platform_->DrainTasks(isolate);
  }

  void CancelVMTasks(Isolate* isolate) {
    platform_->CancelPendingDelayedTasks(isolate);
  }

#if HAVE_INSPECTOR
  bool StartInspector(Environment* env, const char* script_path,
                      std::shared_ptr<DebugOptions> options) {
    // Inspector agent can't fail to start, but if it was configured to listen
    // right away on the websocket port and fails to bind/etc, this will return
    // false.
    return env->inspector_agent()->Start(
        script_path == nullptr ? "" : script_path, options);
  }

  bool InspectorStarted(Environment* env) {
    return env->inspector_agent()->IsListening();
  }
#endif  // HAVE_INSPECTOR

  void StartTracingAgent() {
    if (per_process_opts->trace_event_categories.empty()) {
      tracing_file_writer_ = tracing_agent_->DefaultHandle();
    } else {
      tracing_file_writer_ = tracing_agent_->AddClient(
          ParseCommaSeparatedSet(per_process_opts->trace_event_categories),
          std::unique_ptr<tracing::AsyncTraceWriter>(
              new tracing::NodeTraceWriter(
                  per_process_opts->trace_event_file_pattern)),
          tracing::Agent::kUseDefaultCategories);
    }
  }

  void StopTracingAgent() {
    tracing_file_writer_.reset();
  }

  tracing::AgentWriterHandle* GetTracingAgentWriter() {
    return &tracing_file_writer_;
  }

  NodePlatform* Platform() {
    return platform_;
  }

  std::unique_ptr<tracing::Agent> tracing_agent_;
  tracing::AgentWriterHandle tracing_file_writer_;
  NodePlatform* platform_;
#else  // !NODE_USE_V8_PLATFORM
  void Initialize(int thread_pool_size) {}
  void Dispose() {}
  void DrainVMTasks(Isolate* isolate) {}
  void CancelVMTasks(Isolate* isolate) {}

  void StartTracingAgent() {
    if (!trace_enabled_categories.empty()) {
      fprintf(stderr, "Node compiled with NODE_USE_V8_PLATFORM=0, "
                      "so event tracing is not available.\n");
    }
  }
  void StopTracingAgent() {}

  tracing::AgentWriterHandle* GetTracingAgentWriter() {
    return nullptr;
  }

  NodePlatform* Platform() {
    return nullptr;
  }
#endif  // !NODE_USE_V8_PLATFORM

#if !NODE_USE_V8_PLATFORM || !HAVE_INSPECTOR
  bool InspectorStarted(Environment* env) {
    return false;
  }
#endif  //  !NODE_USE_V8_PLATFORM || !HAVE_INSPECTOR
} v8_platform;

#ifdef __POSIX__
static const unsigned kMaxSignal = 32;
#endif

// Legacy MakeCallback()s

static void WaitForInspectorDisconnect(Environment* env) {
#if HAVE_INSPECTOR
  if (env->inspector_agent()->IsActive()) {
    // Restore signal dispositions, the app is done and is no longer
    // capable of handling signals.
#if defined(__POSIX__) && !defined(NODE_SHARED_MODE)
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    for (unsigned nr = 1; nr < kMaxSignal; nr += 1) {
      if (nr == SIGKILL || nr == SIGSTOP || nr == SIGPROF)
        continue;
      act.sa_handler = (nr == SIGPIPE) ? SIG_IGN : SIG_DFL;
      CHECK_EQ(0, sigaction(nr, &act, nullptr));
    }
#endif
    env->inspector_agent()->WaitForDisconnect();
  }
#endif
}


inline struct node_module* FindModule(struct node_module* list,
                                      const char* name,
                                      int flag) {
  struct node_module* mp;

  for (mp = list; mp != nullptr; mp = mp->nm_link) {
    if (strcmp(mp->nm_modname, name) == 0)
      break;
  }

  CHECK(mp == nullptr || (mp->nm_flags & flag) != 0);
  return mp;
}

node_module* get_builtin_module(const char* name);
node_module* get_internal_module(const char* name);
node_module* get_linked_module(const char* name);

class DLib {
 public:
#ifdef __POSIX__
  static const int kDefaultFlags = RTLD_LAZY;
#else
  static const int kDefaultFlags = 0;
#endif

  inline DLib(const char* filename, int flags)
      : filename_(filename), flags_(flags), handle_(nullptr) {}

  inline bool Open();
  inline void Close();
  inline void* GetSymbolAddress(const char* name);

  const std::string filename_;
  const int flags_;
  std::string errmsg_;
  void* handle_;
#ifndef __POSIX__
  uv_lib_t lib_;
#endif
 private:
  DISALLOW_COPY_AND_ASSIGN(DLib);
};


#ifdef __POSIX__
bool DLib::Open() {
  handle_ = dlopen(filename_.c_str(), flags_);
  if (handle_ != nullptr)
    return true;
  errmsg_ = dlerror();
  return false;
}

void DLib::Close() {
  if (handle_ == nullptr) return;
  dlclose(handle_);
  handle_ = nullptr;
}

void* DLib::GetSymbolAddress(const char* name) {
  return dlsym(handle_, name);
}
#else  // !__POSIX__
bool DLib::Open() {
  int ret = uv_dlopen(filename_.c_str(), &lib_);
  if (ret == 0) {
    handle_ = static_cast<void*>(lib_.handle);
    return true;
  }
  errmsg_ = uv_dlerror(&lib_);
  uv_dlclose(&lib_);
  return false;
}

void DLib::Close() {
  if (handle_ == nullptr) return;
  uv_dlclose(&lib_);
  handle_ = nullptr;
}

void* DLib::GetSymbolAddress(const char* name) {
  void* address;
  if (0 == uv_dlsym(&lib_, name, &address)) return address;
  return nullptr;
}
#endif  // !__POSIX__

static void StartInspector(Environment* env, const char* path,
                           std::shared_ptr<DebugOptions> debug_options) {
#if HAVE_INSPECTOR
  CHECK(!env->inspector_agent()->IsListening());
  v8_platform.StartInspector(env, path, debug_options);
#endif  // HAVE_INSPECTOR
}


extern void PlatformInit();

void ProcessArgv(std::vector<std::string>* args,
                 std::vector<std::string>* exec_args,
                 bool is_env);


void Init(std::vector<std::string>* argv,
          std::vector<std::string>* exec_argv);

void RunBeforeExit(Environment* env);

Environment* CreateEnvironment(IsolateData* isolate_data,
                               Local<Context> context,
                               int argc,
                               const char* const* argv,
                               int exec_argc,
                               const char* const* exec_argv);

static void ReportException(Environment* env, const TryCatch& try_catch) {
  ReportException(env, try_catch.Exception(), try_catch.Message());
}


// Executes a str within the current v8 context.
static MaybeLocal<Value> ExecuteString(Environment* env,
                                       Local<String> source,
                                       Local<String> filename) {
  EscapableHandleScope scope(env->isolate());
  TryCatch try_catch(env->isolate());

  // try_catch must be nonverbose to disable FatalException() handler,
  // we will handle exceptions ourself.
  try_catch.SetVerbose(false);

  ScriptOrigin origin(filename);
  MaybeLocal<v8::Script> script =
      v8::Script::Compile(env->context(), source, &origin);
  if (script.IsEmpty()) {
    ReportException(env, try_catch);
    env->Exit(3);
    return MaybeLocal<Value>();
  }

  MaybeLocal<Value> result = script.ToLocalChecked()->Run(env->context());
  if (result.IsEmpty()) {
    if (try_catch.HasTerminated()) {
      env->isolate()->CancelTerminateExecution();
      return MaybeLocal<Value>();
    }
    ReportException(env, try_catch);
    env->Exit(4);
    return MaybeLocal<Value>();
  }

  return scope.Escape(result.ToLocalChecked());
}


inline node_context_struct *Setup(Isolate* isolate, IsolateData* isolate_data,
                 const std::vector<std::string>& args,
                 const std::vector<std::string>& exec_args) {
  HandleScope handle_scope(isolate);
  Local<Context> context = NewContext(isolate);
  Context::Scope context_scope(context);
  Environment *env =
    new Environment(isolate_data, context, v8_platform.GetTracingAgentWriter());
  env->Start(args, exec_args, v8_is_profiling);

  if (env->options()->debug_options->inspector_enabled &&
      !v8_platform.InspectorStarted(env)) {
    return NULL;  // Signal internal error.
  }

  auto context_struct = new node_context_struct({isolate, env, NULL, NULL});

  env->set_abort_on_uncaught_exception(abort_on_uncaught_exception);

  // TODO(addaleax): Maybe access this option directly instead of setting
  // a boolean member of Environment. Ditto below for trace_sync_io.
  if (env->options()->no_force_async_hooks_checks) {
    env->async_hooks()->no_force_checks();
  }

  {
    Environment::AsyncCallbackScope callback_scope(env);
    env->async_hooks()->push_async_ids(1, 0);
    LoadEnvironment(env);
    env->async_hooks()->pop_async_id(1);
  }

  env->set_trace_sync_io(env->options()->trace_sync_io);

  const char* path = args.size() > 1 ? args[1].c_str() : nullptr;
  StartInspector(env, path, env->options()->debug_options);
  return context_struct;
}

int Teardown(Environment *env) {
  env->set_trace_sync_io(false);

  const int exit_code = EmitExit(env);

  WaitForInspectorDisconnect(env);

  env->set_can_call_into_js(false);
  env->stop_sub_worker_contexts();
  uv_tty_reset_mode();
  env->RunCleanup();
  RunAtExit(env);
  delete env;

  return exit_code;
}

inline node_context_struct *Setup(uv_loop_t* event_loop,
                 const std::vector<std::string>& args,
                 const std::vector<std::string>& exec_args) {
  node_context_struct *context_struct;
  auto allocator0 = CreateArrayBufferAllocator();
  auto allocator = CreateArrayBufferAllocator();
  Isolate* const isolate = NewIsolate(allocator);
  if (isolate == nullptr)
    return NULL;  // Signal internal error.

  {
    Mutex::ScopedLock scoped_lock(node_isolate_mutex);
    CHECK_NULL(node_isolate);
    node_isolate = isolate;
  }

  IsolateData *isolate_data;

  {
    Locker locker(isolate);
    Isolate::Scope isolate_scope(isolate);
    HandleScope handle_scope(isolate);
    isolate_data = CreateIsolateData(
            isolate,
            event_loop,
            v8_platform.Platform(),
            allocator);
    // TODO(addaleax): This should load a real per-Isolate option, currently
    // this is still effectively per-process.
    if (isolate_data->options()->track_heap_objects) {
      isolate->GetHeapProfiler()->StartTrackingHeapObjects(true);
    }
    context_struct =
        Setup(isolate, isolate_data, args, exec_args);
    context_struct->allocator  = allocator0;
    context_struct->isolate_data = isolate_data;
  }
  return context_struct;
}

inline int Teardown(node_context_struct *context_struct) {
  int exit_code;
  Isolate *isolate = context_struct->isolate;
  {
    Locker locker(context_struct->isolate);
    exit_code = Teardown(context_struct->env);

    v8_platform.DrainVMTasks(isolate);
    v8_platform.CancelVMTasks(isolate);
#if defined(LEAK_SANITIZER)
     __lsan_do_leak_check();
#endif
  }

  {
    Mutex::ScopedLock scoped_lock(node_isolate_mutex);
    CHECK_EQ(node_isolate, isolate);
    node_isolate = nullptr;
  }

  isolate->Dispose();
  FreeIsolateData(context_struct->isolate_data);
  FreeArrayBufferAllocator(context_struct->allocator);
  delete context_struct;

  return exit_code;
}

void Create() {
  atexit([] () { uv_tty_reset_mode(); });
  PlatformInit();
  performance::performance_node_start = PERFORMANCE_NOW();

  // Hack around with the argv pointer. Used for process.title = "blah".
#if HAVE_OPENSSL
  {
    std::string extra_ca_certs;
    if (SafeGetenv("NODE_EXTRA_CA_CERTS", &extra_ca_certs))
      crypto::UseExtraCaCerts(extra_ca_certs);
  }
#ifdef NODE_FIPS_MODE
  // In the case of FIPS builds we should make sure
  // the random source is properly initialized first.
  OPENSSL_init();
#endif  // NODE_FIPS_MODE
  // V8 on Windows doesn't have a good source of entropy. Seed it from
  // OpenSSL's pool.
  V8::SetEntropySource(crypto::EntropySource);
#endif  // HAVE_OPENSSL

  v8_platform.Initialize(
      per_process_opts->v8_thread_pool_size);
  V8::Initialize();
  performance::performance_v8_start = PERFORMANCE_NOW();
  v8_initialized = true;
}

void Dispose() {
  v8_platform.StopTracingAgent();
  v8_initialized = false;
  V8::Dispose();

  // uv_run cannot be called from the time before the beforeExit callback
  // runs until the program exits unless the event loop has any referenced
  // handles after beforeExit terminates. This prevents unrefed timers
  // that happen to terminate during shutdown from being run unsafely.
  // Since uv_run cannot be called, uv_async handles held by the platform
  // will never be fully cleaned up.
  v8_platform.Dispose();
}

}  // namespace node

#ifdef __cplusplus
extern "C" {
#endif

node_context *nodeSetup(int argc, char** argv) {
  CHECK_GT(argc, 0);
  argv = uv_setup_args(argc, argv);

  std::vector<std::string> args(argv, argv + argc);
  std::vector<std::string> exec_args;

  // add "-e ''" to args if no execute is provided and not explicitly
  // interactive
  bool default_interactive = true;
  for (auto const& arg: args) {
    if (!arg.compare("-") && !arg.compare("-i") && 
        !arg.compare("--interactive") && !arg.compare("-e") && 
        !arg.compare("--eval"))
      default_interactive = false;
  }
  if (default_interactive) {
    args.push_back(std::string("-e"));
    args.push_back(std::string(""));
  }

  // This needs to run *before* V8::Initialize().
  node::Init(&args, &exec_args);

  node::Create();
  node_context* context = node::Setup(uv_default_loop(), args, exec_args);

  if (!context) node::Dispose();

  return context;
}

void nodeExecuteString(node_context_struct *node_context,
                          const char *source,
                          const char *filename) {

  v8::Isolate *isolate = node_context->isolate;
  node::Environment *env = node_context->env;
  v8::Locker locker(isolate);

  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = node::NewContext(isolate);
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> sourceString =
      v8::String::NewFromUtf8(isolate, source, v8::NewStringType::kNormal)
          .ToLocalChecked();
  v8::Local<v8::String> filenameString =
      v8::String::NewFromUtf8(isolate, filename, v8::NewStringType::kNormal)
          .ToLocalChecked();
  // TODO(rubys) capture and return result:
  ExecuteString(env, sourceString, filenameString);

  {
    v8::SealHandleScope seal(isolate);
    bool more;
    env->performance_state()->Mark(
        node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_START);
    do {
      uv_run(env->event_loop(), UV_RUN_DEFAULT);

      node::v8_platform.DrainVMTasks(isolate);

      more = uv_loop_alive(env->event_loop());
      if (more)
        continue;

      RunBeforeExit(env);

      // Emit `beforeExit` if the loop became alive either after emitting
      // event, or after running some callbacks.
      more = uv_loop_alive(env->event_loop());
    } while (more == true);
    env->performance_state()->Mark(
        node::performance::NODE_PERFORMANCE_MILESTONE_LOOP_EXIT);
  }
}

extern int nodeTeardown(node_context_struct *context_struct) {
  int exit_code = node::Teardown(context_struct);
  node::Dispose();
  return exit_code;
}

#ifdef __cplusplus
}
#endif
