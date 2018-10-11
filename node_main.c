#include "node_embed.h"

int main(int argc, char** argv) {
  struct node_context_struct *context_struct = nodeSetup(argc, argv);

  if (context_struct) {
    nodeExecuteString(context_struct, "let foo = 1", "__init__");
    nodeExecuteString(context_struct, "foo = 2", "__init__");
    nodeExecuteString(context_struct, "console.log(foo)", "__init__");

    return nodeTeardown(context_struct);
  } else {
    return 12;
  }
}
