// node_embed.h : an API for Node embedders

/*

 ______   _______    _        _______ _________            _______  _______
(  __  \ (  ___  )  ( (    /|(  ___  )\__   __/  |\     /|(  ____ \(  ____ \
| (  \  )| (   ) |  |  \  ( || (   ) |   ) (     | )   ( || (    \/| (    \/
| |   ) || |   | |  |   \ | || |   | |   | |     | |   | || (_____ | (__
| |   | || |   | |  | (\ \) || |   | |   | |     | |   | |(_____  )|  __)
| |   ) || |   | |  | | \   || |   | |   | |     | |   | |      ) || (
| (__/  )| (___) |  | )  \  || (___) |   | |     | (___) |/\____) || (____/\
(______/ (_______)  |/    )_)(_______)   )_(     (_______)\_______)(_______/

This header is beyond experimental status at this point.  Change is not only
likely, it is all but certain.  At a minimum, it is planned to become
N-API'ized, but more significantly the immediate plans are to focus on teasing
apart the per-process setup and teardown away from the per isolate setup and
teardown, and that likely will require substantial changes.

*/

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
  node::Environment *env;
  node::ArrayBufferAllocator *allocator;
};
#endif
