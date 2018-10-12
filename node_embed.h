// node_embed.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct node_context_struct node_context;

node_context *nodeSetup(int argc, char** argv);

void nodeExecuteString(node_context *context, const char* string,
  const char *fileName);

int nodeTeardown(node_context *context);

#ifdef __cplusplus
}

#include "node.h"

struct node_context_struct {
  v8::Isolate *isolate;
  node::Environment *env;
  node::IsolateData *isolate_data;
  node::ArrayBufferAllocator *allocator;
};
#endif
