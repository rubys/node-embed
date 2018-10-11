/*
  The node build process is a bit, ahem, complex.  Thie extracts the necessary
  bits from the generated makefiles to build a standalone Makefile,
*/

var fs = require("fs");
var srcdir = process.argv[2] || process.env.HOME + "/git/node";

var input = fs.readFileSync(
  srcdir + "/out/node_lib.target.mk",
  "utf8"
);

var out = "srcdir := " + srcdir + "\n" + "builddir := $(srcdir)/out/Release\n" +
 "obj := $(builddir)/obj\n" + "\n";
out += input.match(/DEFS_Release.*?\n\n/s)[0];
out += input.match(/INCS_Release.*?\n\n/s)[0];
out += input.match(/CFLAGS_Release.*?\n\n/s)[0];
out += input.match(/CFLAGS_CC_Release.*?\n\n/s)[0];

input = fs.readFileSync(srcdir + "/out/node.target.mk", "utf8");
out += input.match(/LDFLAGS_Release.*?\n\n/s)[0];
out += input.match(/LIBS.*?\n\n/s)[0];
out += input.match(/LD_INPUTS.*?\n/)[0] + "\n";

out += "node_main: node_main.o node.o\n" + "\tc++ $(LDFLAGS_Release) -o $@ $+ $(LIBS) $(LD_INPUTS)\n\n";
out += "node.o: node.cc node_embed.h\n" + "\tc++ $(DEFS_Release) $(INCS_Release) $(CFLAGS_Release) \\\n" + "\t$(CFLAGS_CC_Release) -c $< -o $@\n\n";
out += "node_main.o: node_main.c node_embed.h\n" + "\tcc $(CFLAGS_Release) -c $< -o $@\n";

fs.writeFileSync("Makefile", out)
