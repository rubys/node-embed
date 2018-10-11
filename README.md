The goal of this codebase is to help anchor a discussion of what an
embeddable interface to Node.js would look like with running code.

Current state:
-------------

* Embedding node.js today is not only a desirable goal, it is actively being
done by a number of projects despite the lack of support.
* Lack of support starts with lack of documenation.
* The one stable interface `node::Start()` is a misnomer as it not only starts
node, but it also executes, and terminates node.  This rarely is what an embedder wants.
* Embedding node today requires extracting key bits from the source files, and
knowledge of the build system, platforms, and dependencies (v8, openssl, etc).

Quick start:
-----------

* To start with, one needs to clone and build node from github.  This code
has been tested on Mac OS/X, but is likely to work on Linux and other
Unix like operating systems.
* The next step is to run the provided `mkmf.js` and specify the directory
for the built node tree.  This builds a standalone Makefile for your platform.
* The final step is to run `make`.  This will build an executable named
`./node_main` which you can run.

How it works:
------------

* The `mkmf.js` is pretty self explanatory.
* `node.cc` is an updated version of `src/node.cc` from the node repository
with the following changes:
    * First, it is a subset so as to not cause duplicate symbols when linked
      with `libnode.a`.  This part is done for demo purposes only, the intent
      is that this will be abandoned, and the results of the next two steps
      will be foled into node.js itself.
    * Second, `node::Start` has been split into `node:Setup`,
      `node::ExecuteString` and `node::Teardown`.
    * Third, a `C` interface to invoking these functions is provided.
* `node_embed.h` contains the C API
* `node_main.c` is a demo program that calls `nodeSetup`, successively calls
  `nodeExecuteString` and then finally `nodeTeardown`.

Why was this demo created?
-------------------------

* I believe that creating a standalone Makefile and application should be
a part of the CI for node.js.  This demo shows how it could be done for
discussion purposes before it is implemented "for real".


Discussion starters:
-------------------

* Should we retire `node::Start` or implement it by calling these new wrappers?
* Is argc/argv the right interface for an API?  Should the defaults be
  different (example: should no arguments trigger a REPL) when called by an API?
* Splitting `node::Start` into three steps means that some data is allocated
  on the heap instead of the stack, and some things (most notabley locks)
  are obtained and released multiple times.  My intuition is that this
  is negligable when compared to the overhead of starting a complete node
  executable, but that needs to be measured.
* Currently, `nodeExecuteString` is synchronous is that it waits until the
  event loop is emptied before returning.  Should an asyncronous version
  be created, and if so, what is the desired semantics?

Future plans:
------------

* This demo only shows incrementally updating a single execution context.
It should be expanded to include creating multiple contexts (either
concurrently or serially).
* The iterface should be NAPIized, and make to work on Microsoft Windows.
Help from the ChakraCore team would be greatly appreciated.
* While I suspect that only the `C` interface needs to be officially maintained,
prodiving examples in Go, Java, Python, TCL, Ruby, and others would be valuable and straightforward.


