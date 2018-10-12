#include "node_embed.h"

int main(int argc, char** argv) {
  node_context *context = nodeSetup(argc, argv);

  if (context) {
    nodeExecuteString(context, "let foo = 1", "__init__");
    nodeExecuteString(context, "foo = 2", "__init__");
    nodeExecuteString(context, "console.log(foo)", "__init__");

    return nodeTeardown(context);
  } else {
    return 12;
  }
}
