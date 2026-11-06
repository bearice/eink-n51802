// Dump every function's decompiled C to a file, so the e-ink driver can be
// located by grepping for GPIO register addresses.
// @category Dump

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import java.io.PrintWriter;

public class DumpAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = System.getenv("GHIDRA_DUMP");
        if (out == null) {
            out = "C:\\Users\\bearice\\Workspace\\eink-n51802\\ghidra_dump.c";
        }
        PrintWriter w = new PrintWriter(out, "UTF-8");
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        for (Function f : fm.getFunctions(true)) {
            w.println("// ==== " + f.getName() + " @ " + f.getEntryPoint()
                      + " size=" + f.getBody().getNumAddresses());
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            if (r != null && r.decompileCompleted()) {
                w.println(r.getDecompiledFunction().getC());
            } else {
                w.println("// decompile failed");
            }
            w.println();
            n++;
        }
        w.close();
        d.dispose();
        println("dumped " + n + " functions to " + out);
    }
}
