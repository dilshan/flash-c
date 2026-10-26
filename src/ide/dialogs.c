/* Flash C IDE - Options dialogs, search dialogs and other dialog boxes */
#include "ide.h"
#include <ctype.h>
#include <unistd.h>

#define H_OK "Accept the settings in this dialog box"

/* run a settings dialog transactionally: Cancel restores the configuration */
static int opt_run(Dialog *d, const char *helptopic)
{
    static Config bak;
    bak = cfg;
    uint8_t palbak[P_COUNT]; memcpy(palbak, pal, sizeof pal);
    int r;
    do {
        r = dlg_run(d);
        if (r == cmHelp) help_show(helptopic ? helptopic : "Compiler options");
    } while (r == cmHelp);
    if (r != cmOK) { cfg = bak; memcpy(pal, palbak, sizeof pal); }
    return r == cmOK;
}

static void radios(Dialog *d, int *val, const char **names, int n, const char **hints)
{
    for (int i = 0; i < n; i++) db_radio(d, names[i], val, i, hints ? hints[i] : NULL);
}

/* ---------- warning check lists ---------- */
static void warn_dialog(const char *title, const char **names, const int *idx, int n, int width)
{
    Dialog d; db_start(&d, title, width);
    for (int i = 0; i < n; i++) db_check(&d, names[i], &cfg.warn[idx[i]], 0, "Select this message to be displayed as a warning");
    d.cur_y += 0;
    db_finish(&d, 1);
    if (opt_run(&d, "Messages and errors")) { }
}

static void msg_dialog_display(void)
{
    Dialog d; db_start(&d, " Compiler Messages ", 58);
    db_num(&d, "~E~rrors:   Stop After", &cfg.max_errors, 8, "Stop compiling after detecting a specified number of errors");
    db_num(&d, "~W~arnings: Stop After", &cfg.max_warnings, 8, "Stop compiling after detecting a specified number of warnings");
    db_group(&d, "Display warnings", 3);
    db_radio(&d, "~S~elected", &cfg.warn_display, 1, "Display only selected warning messages");
    db_radio(&d, "~A~ll", &cfg.warn_display, 2, "Display all warning messages");
    db_radio(&d, "~N~one", &cfg.warn_display, 0, "Suppress all warning messages");
    db_end_group(&d);
    db_finish(&d, 1);
    opt_run(&d, "Messages and errors");
}

void opt_command(int cmd)
{
    Dialog d;
    switch (cmd) {
    case cmOptApplication: {
        db_start(&d, " Set Application Options ", 56);
        db_group(&d, "Target", 2);
        db_radio(&d, "~C~onsole executable (ELF)", &cfg.app_target, 0, "Generate a native Linux executable");
        db_radio(&d, "~L~ibrary", &cfg.app_target, 1, "Generate a library (not supported yet)");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptCodeGen: {
        db_start(&d, " Code Generation ", 80);
        db_at(&d, 2, 2, 32);
        db_group(&d, "Model", 6);
        const char *mn[] = { "~T~iny", "~S~mall", "~M~edium", "Co~m~pact", "~L~arge", "~H~uge" };
        const char *mh[] = { "Use smallest memory model (64K available for code + data)", "Use small memory model (64K for code, 64K for static data)", "Use medium memory model", "Use compact memory model", "Use large memory model", "Use largest memory model" };
        radios(&d, &cfg.model, mn, 6, mh);
        db_end_group(&d);
        db_group(&d, "Options", 5);
        db_check(&d, "Treat ~e~nums as ints", &cfg.enums_as_ints, 0, "Treat enums as int - i.e. don't shorten to char when possible");
        db_check(&d, "~W~ord alignment", &cfg.word_align, 0, "Align noncharacter data at even addresses");
        db_check(&d, "Du~p~licate strings merged", &cfg.merge_strings, 0, "Merge two strings when one matches another");
        db_check(&d, "~U~nsigned characters", &cfg.unsigned_char, 0, "Treat all char declarations as if they were unsigned char type");
        db_check(&d, "Pre-c~o~mpiled headers", &cfg.precompiled, 0, "Generate and use pre-compiled headers");
        db_end_group(&d);
        int ly = d.cur_y;
        db_at(&d, 35, 2, 30);
        db_group(&d, "Assume SS Equals DS", 3);
        db_radio(&d, "De~f~ault for memory model", &cfg.assume_ss, 0, "Memory model determines whether to assume SS and DS are equal");
        db_radio(&d, "~N~ever", &cfg.assume_ss, 1, "Generate code that doesn't assume SS and DS are equal");
        db_radio(&d, "Alwa~y~s", &cfg.assume_ss, 2, "Generate code that assumes SS and DS are equal");
        db_end_group(&d);
        db_group(&d, "Floating Point", 4);
        const char *fn[] = { "No~n~e", "~E~mulation", "~8~087", "8028~7~" };
        const char *fh[] = { "Don't use floating-point numbers", "Detect and use coprocessor, otherwise emulate", "Generate direct 8087 inline code", "Generate direct 80287 inline code" };
        radios(&d, &cfg.fp, fn, 4, fh);
        db_end_group(&d);
        db_group(&d, "Instruction Set", 3);
        const char *in[] = { "8088/808~6~", "80~1~86", "80~2~86" };
        const char *ih[] = { "Generate code for the 8088/8086 instruction set only", "Use 80188/186 enhanced instructions", "Use 80286 enhanced instructions" };
        radios(&d, &cfg.instr, in, 3, ih);
        db_end_group(&d);
        if (d.cur_y < ly) d.cur_y = ly;
        db_at(&d, 2, d.cur_y, 74);
        db_input(&d, "~D~efines", cfg.defines, sizeof cfg.defines, 60, "Pass macro definitions to the preprocessor (NAME or NAME=VALUE, separated by semicolons)");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptAdvCodeGen: {
        db_start(&d, " Advanced Code Generation ", 74); d.bw = 32;
        db_at(&d, 2, 2, 32);
        db_group(&d, "Options", 7);
        db_check(&d, "Generate ~u~nderbars", &cfg.under_scores, 0, "Add underscore character to start of global identifiers");
        db_check(&d, "~L~ine numbers debug info", &cfg.line_numbers, 0, "Include line numbers in the object map file");
        db_check(&d, "~D~ebug info in OBJs", &cfg.debug_info, 0, "Include debugging information in object files");
        db_check(&d, "~F~ast floating point", &cfg.fast_fp, 0, "Generate code for fast floating-point operations");
        db_check(&d, "Fast huge po~i~nters", &cfg.fast_huge, 0, "Use fast huge pointer arithmetic");
        db_check(&d, "Generate CO~M~DEFs", &cfg.comdefs, 0, "Generate COMDEFs for uninitialized global data");
        db_check(&d, "~A~utomatic far data", &cfg.auto_far, 0, "Place data objects exceeding the threshold in far data segments");
        db_end_group(&d);
        db_num(&d, "Far Data ~T~hreshold", &cfg.far_threshold, 8, "Automatic far data generation minimum size");
        db_at(&d, 36, 2, 20);
        db_group(&d, "C++ Virtual Tables", 4);
        const char *vn[] = { "~S~mart", "~L~ocal", "~E~xternal", "~P~ublic" };
        radios(&d, &cfg.virt_tables, vn, 4, NULL);
        db_end_group(&d);
        db_group(&d, "Template Generation", 3);
        const char *tn[] = { "S~m~art", "Ex~t~ernal", "~G~lobal" };
        radios(&d, &cfg.templ_gen, tn, 3, NULL);
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptCpp: {
        db_start(&d, " C++ Options ", 62);
        db_group(&d, "Use C++ Compiler", 2);
        db_radio(&d, "CPP e~x~tension", &cfg.cpp_ext, 0, "Compile files with a C++ extension as C++");
        db_radio(&d, "~C~++ always", &cfg.cpp_ext, 1, "Always compile as C++");
        db_end_group(&d);
        db_group(&d, "Options", 2);
        db_check(&d, "~O~ut-of-line inline functions", &cfg.cpp_inline, 0, "Do not expand inline functions");
        db_check(&d, "~F~ar virtual tables", &cfg.cpp_far_vt, 0, "Place virtual tables in far memory");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptEntryExit: {
        db_start(&d, " Entry/Exit Code Generation ", 60);
        db_group(&d, "Prolog/Epilog Code Generation", 2);
        db_radio(&d, "~S~tandard", &cfg.entry_code, 0, "Generate standard prolog/epilog code");
        db_radio(&d, "~O~verlay", &cfg.entry_code, 1, "Generate prolog/epilog code that can be overlaid");
        db_end_group(&d);
        db_group(&d, "Calling Convention", 2);
        db_radio(&d, "~C~", &cfg.c_calls, 0, "Use C calling convention by default");
        db_radio(&d, "~P~ascal", &cfg.c_calls, 1, "Use Pascal calling convention by default");
        db_end_group(&d);
        db_group(&d, "Stack Options", 2);
        db_check(&d, "Standard stack ~f~rame", &cfg.std_frame, 0, "Generate standard stack frame entry and exit code on each function");
        db_check(&d, "Test stack o~v~erflow", &cfg.stack_check, 0, "Check for a stack overflow at run time");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptOptimize: {
        db_start(&d, " Optimization Options ", 60);
        db_group(&d, "Optimizations", 2);
        db_check(&d, "~S~uppress Redundant Loads", &cfg.opt_redundant, 0, "Fold constants at compile time");
        db_check(&d, "~J~ump optimization", &cfg.opt_jump, 0, "Eliminate redundant jumps and optimize branches");
        db_end_group(&d);
        db_group(&d, "Optimize For", 2);
        db_radio(&d, "Si~z~e", &cfg.opt_for_speed, 0, "Optimize code to favor smaller size");
        db_radio(&d, "S~p~eed", &cfg.opt_for_speed, 1, "Optimize code to favor faster execution speed");
        db_end_group(&d);
        db_group(&d, "Register Variables", 3);
        db_radio(&d, "~N~one", &cfg.regvars, 0, "Don't use register variables");
        db_radio(&d, "Register key~w~ord", &cfg.regvars, 1, "Allocate register variables only when explicitly requested");
        db_radio(&d, "~A~utomatic", &cfg.regvars, 2, "Let the compiler automatically determine when to use registers");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptSource: {
        db_start(&d, " Source Options ", 60);
        db_check(&d, "~N~ested comments", &cfg.nested_comments, 0, "Allow nested comments in source files");
        db_gap(&d);
        db_group(&d, "Keywords", 4);
        const char *kn[] = { "~E~xtended (near, far ...)", "~A~NSI", "~U~NIX V", "Kernighan and ~R~itchie" };
        const char *kh[] = { "Recognize the extended keywords", "Recognize only ANSI keywords", "Recognize UNIX System V keywords", "Recognize keywords from the original K & R definition of C" };
        radios(&d, &cfg.keywords, kn, 4, kh);
        db_end_group(&d);
        db_num(&d, "~I~dentifier Length", &cfg.ident_len, 6, "Enter significant length of identifiers");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptMsgDisplay: msg_dialog_display(); break;
    case cmOptMsgPortability: {
        const char *n[] = { "~N~on-portable pointer conversion", "Non-portable pointer ~c~omparison", "Constant out of ~r~ange in comparison", "Constant is ~l~ong", "Conversion may lose ~s~ignificant digits", "~M~ixing pointers to signed and unsigned char" };
        int idx[] = { 0, 1, 2, 3, 4, 5 };
        warn_dialog(" Portability Warnings ", n, idx, 6, 66);
        break;
    }
    case cmOptMsgAnsi: {
        const char *n[] = { "~V~oid functions may not return a value", "~B~oth return and return of a value used", "~S~uspicious pointer conversion", "~U~ndefined structure 'ident'", "~R~edefinition of 'ident' is not identical", "~H~exadecimal value more than three digits", "Bit ~f~ields must be signed or unsigned int", "'ident' declared as both e~x~ternal and static", "~D~eclare 'ident' prior to use in prototype", "Division by ~z~ero", "~I~nitializing 'ident' with 'ident'", "Initialization is only ~p~artially bracketed" };
        int idx[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
        warn_dialog(" ANSI Violations ", n, idx, 12, 66);
        break;
    }
    case cmOptMsgCpp: {
        const char *n[] = { "~B~ase initialization without a class name is obsolete", "Functions containing 'ident' are not e~x~panded inline", "Temporary used to ~i~nitialize 'ident'", "~T~emporary used for parameter 'ident'", "Constant ~m~ember 'ident' is not initialized", "This ~s~tyle of function definition is now obsolete", "~U~se of 'overload' is now unnecessary and obsolete", "Assi~g~ning 'type' to 'enumeration'", "'function1' ~h~ides virtual function 'function2'", "~N~on-const function 'ident' called for const object", "Base ~c~lass 'ident' is inaccessible because also in 'ident'", "~O~verloaded prefix operator used as a postfix operator", "Array size for '~d~elete' ignored", "Use ~q~ualified name to access nested type 'ident'" };
        int idx[] = { 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
        warn_dialog(" C++ Warnings ", n, idx, 14, 74);
        break;
    }
    case cmOptMsgFrequent: {
        const char *n[] = { "~F~unction should return a value", "~U~nreachable code", "~C~ode has no effect", "~P~ossible use of 'ident' before definition", "'~i~dent' is assigned a value which is never used", "Parameter 'ident' is ~n~ever used", "Possibly incorrect ~a~ssignment" };
        int idx[] = { 32, 33, 34, 35, 36, 37, 38 };
        warn_dialog(" Frequent Errors ", n, idx, 7, 66);
        break;
    }
    case cmOptMsgLess: {
        const char *n[] = { "~S~uperfluous & with function", "~A~mbiguous operators need parentheses", "Structure passed by ~v~alue", "~N~o declaration for function 'ident'", "Call to function with no ~p~rototype", "~U~nknown assembler instruction", "I~l~l-formed pragma", "C~o~ndition is always (true/false)", "A~r~ray variable 'ident' is near", "'~i~dent' declared but never used" };
        int idx[] = { 39, 40, 41, 42, 43, 44, 45, 46, 47, 48 };
        warn_dialog(" Less Frequent Errors ", n, idx, 10, 66);
        break;
    }
    case cmOptNames: {
        static const char *lab[12] = { "~C~ode Segment", "Code ~G~roup", "Code C~l~ass", "~D~ata Segment", "D~a~ta Group", "Da~t~a Class", "~B~SS Segment", "BSS G~r~oup", "BSS C~l~ass", "Far Data Segme~n~t", "Far Data Grou~p~", "~F~ar Data Class" };
        db_start(&d, " Segment Names ", 64);
        db_at(&d, 2, 2, 24);
        for (int i = 0; i < 12; i++) {
            if (i == 6) db_at(&d, 30, 2, 24);
            db_input(&d, lab[i], cfg.seg_names[i], 24, 20, "Change the default segment, group, or class name");
        }
        db_finish(&d, 1);
        d.h = 24;
        opt_run(&d, NULL);
        break;
    }
    case cmOptMake: {
        db_start(&d, " Make ", 62);
        db_group(&d, "Break Make On", 4);
        db_check(&d, "~W~arnings", &cfg.brk_warn, 0, "Stop a project-make on any warning message");
        db_check(&d, "~E~rrors", &cfg.brk_err, 0, "Stop a project-make on any error message");
        db_check(&d, "~F~atal errors", &cfg.brk_fatal, 0, "Stop a project-make on any fatal error message");
        db_check(&d, "~A~ll sources processed", &cfg.brk_all, 0, "Stop after all sources have been processed");
        db_end_group(&d);
        db_group(&d, "After Compiling", 3);
        db_radio(&d, "~S~top", &cfg.after_compile, 0, "Stop after compiling");
        db_radio(&d, "~R~un linker", &cfg.after_compile, 1, "Link project items into the executable");
        db_radio(&d, "Run ~l~ibrarian", &cfg.after_compile, 2, "Put project items into a library");
        db_end_group(&d);
        db_check(&d, "~C~heck auto-dependencies", &cfg.check_autodep, 0, "Rebuild when included files change");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptLinkSettings: {
        db_start(&d, " Linker ", 66); d.bw = 28;
        db_at(&d, 2, 2, 26);
        db_group(&d, "Map File", 4);
        const char *mn[] = { "O~f~f", "~S~egments", "~P~ublics", "~D~etailed" };
        radios(&d, &cfg.map_file, mn, 4, NULL);
        db_end_group(&d);
        db_group(&d, "Output", 4);
        const char *on[] = { "S~t~andard EXE", "O~v~erlaid EXE", "~S~tandard Library", "~O~verlaid Library" };
        radios(&d, &cfg.out_kind, on, 4, NULL);
        db_end_group(&d);
        db_at(&d, 30, 2, 24);
        db_group(&d, "Options", 5);
        db_check(&d, "~I~nitialize segments", &cfg.lnk_init_seg, 0, NULL);
        db_check(&d, "Default ~l~ibraries", &cfg.lnk_default_libs, 0, NULL);
        db_check(&d, "~W~arn duplicate symbols", &cfg.lnk_warn_dup, 0, NULL);
        db_check(&d, "\"~N~o stack\" warning", &cfg.lnk_nostack, 0, NULL);
        db_check(&d, "~C~ase-sensitive link", &cfg.lnk_case, 0, NULL);
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptLinkLibs: {
        db_start(&d, " Libraries ", 56);
        db_group(&d, "Libraries", 4);
        db_check(&d, "~G~raphics library", &cfg.lib_graphics, 0, NULL);
        db_check(&d, "Text-~U~I library", &cfg.lib_tvision, 0, NULL);
        db_check(&d, "~C~ontainer library", &cfg.lib_container, 0, NULL);
        db_check(&d, "~S~tandard run-time", &cfg.lib_stdrt, 0, NULL);
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptLibrarian: {
        db_start(&d, " Librarian Options ", 62);
        db_group(&d, "Options", 4);
        db_check(&d, "~G~enerate list file", &cfg.lib_listing, 0, NULL);
        db_check(&d, "~C~ase sensitive library", &cfg.lib_case, 0, NULL);
        db_check(&d, "~P~urge comment records", &cfg.lib_purge, 0, "Purge comment records from modules added to library");
        db_check(&d, "Create ~e~xtended dictionary", &cfg.lib_extdict, 0, NULL);
        db_end_group(&d);
        db_num(&d, "~L~ibrary Page Size", &cfg.lib_pagesize, 6, NULL);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptDebugger: {
        db_start(&d, " Debugger ", 62); d.bw = 24;
        db_at(&d, 2, 2, 24);
        db_group(&d, "Source Debugging", 3);
        const char *sn[] = { "~O~n", "~S~tandalone", "~N~one" };
        radios(&d, &cfg.dbg_source, sn, 3, NULL);
        db_end_group(&d);
        db_group(&d, "Display Swapping", 3);
        const char *wn[] = { "No~n~e", "Sma~r~t", "~A~lways" };
        radios(&d, &cfg.dbg_swap, wn, 3, NULL);
        db_end_group(&d);
        db_at(&d, 28, 2, 22);
        db_group(&d, "Inspectors", 5);
        db_check(&d, "Show ~i~nherited", &cfg.dbg_inherited, 0, NULL);
        db_check(&d, "Show ~m~ethods", &cfg.dbg_methods, 0, NULL);
        db_radio(&d, "Show ~d~ecimal", &cfg.dbg_radix, 0, NULL);
        db_radio(&d, "Show ~h~ex", &cfg.dbg_radix, 1, NULL);
        db_radio(&d, "Show ~b~oth", &cfg.dbg_radix, 2, NULL);
        db_end_group(&d);
        db_num(&d, "~P~rogram Heap Size", &cfg.dbg_heap, 8, NULL);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptDirs: {
        db_start(&d, " Directories ", 72);
        d.bw = 54;
        db_input(&d, "~I~nclude Directories", cfg.incdir, sizeof cfg.incdir, 52, "The directories to search for your include (.h) files");
        db_input(&d, "~L~ibrary Directories", cfg.libdir, sizeof cfg.libdir, 52, "The directories to search for your library files");
        db_input(&d, "~O~utput Directory", cfg.outdir, sizeof cfg.outdir, 52, "The output directory for your executable files");
        db_input(&d, "~S~ource Directories", cfg.srcdir, sizeof cfg.srcdir, 52, "The directories that store source for included files");
        db_finish(&d, 1);
        for (int i = d.n - 3; i < d.n; i++) { d.it[i].x = 30 + (i - (d.n - 3)) * 0; }
        d.it[d.n - 3].x = 58; d.it[d.n - 3].y = 2 + 0; d.it[d.n - 2].x = 58; d.it[d.n - 1].x = 58;
        d.w = 72;
        d.it[d.n - 3].y = 3; d.it[d.n - 2].y = 5; d.it[d.n - 1].y = 7;
        opt_run(&d, NULL);
        break;
    }
    case cmOptPrefs: {
        db_start(&d, " Preferences ", 74); d.bw = 26;
        db_at(&d, 2, 2, 26);
        db_group(&d, "Screen Size", 2);
        db_radio(&d, "~2~5 lines", &cfg.screen_lines, 0, "Use the whole terminal");
        db_radio(&d, "~4~3/50 lines", &cfg.screen_lines, 1, "Use a larger display if the terminal has one");
        db_end_group(&d);
        db_group(&d, "Source Tracking", 2);
        db_radio(&d, "~N~ew window", &cfg.src_tracking, 0, "Open a new window to track messages");
        db_radio(&d, "~C~urrent window", &cfg.src_tracking, 1, "Replace the contents of the current window when tracking messages");
        db_end_group(&d);
        db_group(&d, "Command Set", 3);
        db_radio(&d, "C~U~A", &cfg.cmdset, 0, "Use CUA menus and hot-key selections (Ctrl-X, Ctrl-C, Ctrl-V, Ctrl-Z)");
        db_radio(&d, "~A~lternate", &cfg.cmdset, 1, "Use the classic menus and hot-key selections");
        db_radio(&d, "Na~t~ive", &cfg.cmdset, 2, "Use the classic menus and hot-key selections");
        db_end_group(&d);
        db_at(&d, 30, 2, 26);
        db_group(&d, "Auto-Save", 4);
        db_check(&d, "~E~ditor files", &cfg.as_editor, 0, "Save modified editor files when you run or compile");
        db_check(&d, "En~v~ironment", &cfg.as_env, 0, "Save config file when you quit or run");
        db_check(&d, "~D~esktop", &cfg.as_desktop, 0, "Save desktop when you quit or run");
        db_check(&d, "~P~roject", &cfg.as_project, 0, "Save project when you quit or run");
        db_end_group(&d);
        db_check(&d, "~S~ave old messages", &cfg.save_old_msgs, 0, "Preserve messages before a compile or make");
        db_check(&d, "Pa~u~se after run", &cfg.pause_after_run, 0, "Wait for a key after the program ends");
        db_finish(&d, 1);
        int old = cfg.screen_lines;
        if (opt_run(&d, NULL) && old != cfg.screen_lines) { set_screen_lines(cfg.screen_lines ? 0 : 25); wm_resize(); }
        break;
    }
    case cmOptEditor: {
        db_start(&d, " Editor Options ", 66); d.bw = 46;
        db_group(&d, "Editor Options", 11);
        db_check(&d, "Create backup ~f~iles", &cfg.backup, 0, "Create a backup (.BAK) file whenever you save");
        db_check(&d, "~I~nsert mode", &cfg.insert, 0, "Characters typed are inserted into edit text");
        db_check(&d, "~A~utoindent mode", &cfg.autoindent, 0, "Auto-indent to previous indentation level on each new line");
        db_check(&d, "~U~se tab character", &cfg.use_tab, 0, "Insert actual tab characters into file");
        db_check(&d, "~O~ptimal fill", &cfg.optimal_fill, 0, "Optimally replace consecutive spaces with tabs");
        db_check(&d, "~S~yntax Highlighting", &cfg.syntax, 0, "Syntax elements are highlighted in editor windows");
        db_check(&d, "~B~ackspace unindents", &cfg.bs_unindent, 0, "The Backspace key will unindent, based on previous starting columns");
        db_check(&d, "~C~ursor through tabs", &cfg.cursor_tabs, 0, "Allow cursor to move through tabs one column at a time");
        db_check(&d, "~G~roup undo", &cfg.group_undo, 0, "Group similar editor actions into one Undo action");
        db_check(&d, "~P~ersistent blocks", &cfg.persist_blocks, 0, "Selected text remains highlighted when cursor moves");
        db_check(&d, "Overwrite b~l~ocks", &cfg.overwrite_blocks, 0, "Inserting new text deletes (overwrites) selected text");
        db_end_group(&d);
        db_num(&d, "~T~ab Size", &cfg.tabsize, 6, "Change the number of columns to use for tab width");
        db_input(&d, "~D~efault Extension", cfg.defext, sizeof cfg.defext, 8, "Use this file extension if you don't provide one");
        db_finish(&d, 1);
        opt_run(&d, "The editor");
        break;
    }
    case cmOptMouse: {
        db_start(&d, " Mouse Options ", 62); d.bw = 30;
        db_group(&d, "Right Mouse Button", 10);
        const char *n[] = { "~N~othing", "~T~opic search", "~S~earch", "Search ~a~gain", "~R~eplace", "~G~o to cursor", "~B~reakpoint", "~I~nspect", "~E~valuate", "Add ~w~atch" };
        radios(&d, &cfg.mouse_right, n, 10, NULL);
        db_end_group(&d);
        db_num(&d, "~M~ouse Double Click", &cfg.mouse_dbl, 6, "Adjust the double-click speed of your mouse");
        db_check(&d, "Re~v~erse mouse buttons", &cfg.mouse_reverse, 0, "Make the active mouse button the rightmost one");
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptDesktop: {
        db_start(&d, " Desktop Preferences ", 56);
        db_group(&d, "Save Options", 6);
        db_check(&d, "~H~istory lists", &cfg.sv_history, 0, "Save all history lists");
        db_check(&d, "~C~lipboard", &cfg.sv_clip, 0, "Save Clipboard contents");
        db_check(&d, "~W~atch expressions", &cfg.sv_watch, 0, "Save all watch expressions");
        db_check(&d, "~B~reakpoints", &cfg.sv_bp, 0, "Save all breakpoints");
        db_check(&d, "~O~pen windows", &cfg.sv_open, 0, "Save state information for all open windows");
        db_check(&d, "C~l~osed windows", &cfg.sv_closed, 0, "Save state information for closed windows");
        db_end_group(&d);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptStartup: {
        db_start(&d, " Startup Options ", 62); d.bw = 28;
        db_at(&d, 2, 2, 28);
        db_group(&d, "Video Mode", 3);
        db_radio(&d, "~A~uto detect", &cfg.video_mode, 0, "Autodetect and use the terminal's capabilities");
        db_radio(&d, "~B~lack & White / LCD", &cfg.video_mode, 1, "Use black and white");
        db_radio(&d, "~C~olor", &cfg.video_mode, 2, "Always use color");
        db_end_group(&d);
        db_group(&d, "Video Startup Options", 3);
        db_check(&d, "~S~ave entire palette", &cfg.save_palette, 0, NULL);
        db_check(&d, "~D~ual monitor mode", &cfg.dual_monitor, 0, NULL);
        db_check(&d, "S~n~ow checking", &cfg.snow, 0, NULL);
        db_end_group(&d);
        db_check(&d, "Use E~x~tended Memory", &cfg.use_ext, 0, NULL);
        db_check(&d, "Use ~E~MS Memory", &cfg.use_ems, 0, NULL);
        db_finish(&d, 1);
        opt_run(&d, NULL);
        break;
    }
    case cmOptSave: {
        if (cfg_save()) msgbox(" Error ", "Cannot write the configuration file", MB_OK | MB_ERROR);
        else infobox(" Save Options ", "Options saved to\n%s", cfg_path());
        break;
    }
    case cmOptColors: dlg_colors(); break;
    case cmOptTransfer: dlg_transfer(); break;
    }
}

/* ---------- search dialogs ---------- */
int dlg_find_text(int replace)
{
    Dialog d; dlg_init(&d, replace ? " Replace Text " : " Find Text ");
    d.w = replace ? 66 : 62; d.h = replace ? 20 : 17;
    int r = 0;
    dlg_input(&d, 3, 1, 44, "~T~ext to Find", find_text, sizeof find_text, "Enter literal text or regular expression to search for");
    int y = 3;
    if (replace) { dlg_input(&d, 3, 4, 44, "~N~ew Text", repl_text, sizeof repl_text, "Enter new text that will replace matching text"); y = 6; }
    dlg_frame(&d, 3, y + 1, 26, replace ? 6 : 5, "Options");
    dlg_check(&d, 5, y + 2, "~C~ase sensitive", &find_opts[0], 0, "Case of letters is significant during search");
    dlg_check(&d, 5, y + 3, "~W~hole words only", &find_opts[1], 0, "Consider only whole words during search");
    dlg_check(&d, 5, y + 4, "~R~egular expression", &find_opts[2], 0, "Interpret search text as a regular expression");
    if (replace) dlg_check(&d, 5, y + 5, "~P~rompt on replace", &find_opts[3], 0, "Prompt before replacing text after every match");
    dlg_frame(&d, 31, y + 1, 18, 4, "Direction");
    dlg_radio(&d, 33, y + 2, "Forwar~d~", &find_opts[4], 0, "Search forward from origin");
    dlg_radio(&d, 33, y + 3, "~B~ackward", &find_opts[4], 1, "Search backward from origin");
    int y2 = y + (replace ? 7 : 6);
    dlg_frame(&d, 3, y2, 26, 4, "Scope");
    dlg_radio(&d, 5, y2 + 1, "~G~lobal", &find_opts[5], 0, "Search from origin to the end of the edit buffer");
    dlg_radio(&d, 5, y2 + 2, "~S~elected text", &find_opts[5], 1, "Search current selected block of edit buffer");
    dlg_frame(&d, 31, y2, 18, 4, "Origin");
    dlg_radio(&d, 33, y2 + 1, "~F~rom cursor", &find_opts[6], 0, "Start search from current cursor position");
    dlg_radio(&d, 33, y2 + 2, "~E~ntire scope", &find_opts[6], 1, "Start search at beginning of scope");
    int bx = d.w - 16;
    dlg_button(&d, bx, 2, 14, "OK", cmOK, 1, "Accept the settings in this dialog box");
    if (replace) dlg_button(&d, bx, 5, 14, "Change ~A~ll", cmChangeAll, 0, "Find all occurrences of the search text and replace them");
    dlg_button(&d, bx, replace ? 8 : 5, 14, "Cancel", cmCancel, 0, "Close the dialog box without making any changes");
    dlg_button(&d, bx, replace ? 11 : 8, 14, "Help", cmHelp, 0, "View a Help screen about this dialog box");
    d.it[0].selall = 1;
    do { r = dlg_run(&d); if (r == cmHelp) help_show("The editor"); } while (r == cmHelp);
    return r;
}

int dlg_goto_line(int *line)
{
    char buf[16]; snprintf(buf, sizeof buf, "%d", *line);
    Dialog d; dlg_init(&d, " Go to Line Number ");
    d.w = 40; d.h = 9;
    dlg_input(&d, 3, 1, 20, "~E~nter New Line Number", buf, 12, "Enter Edit window line number to position cursor on");
    dlg_button(&d, 4, 5, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 16, 5, 10, "Cancel", cmCancel, 0, NULL);
    if (dlg_run(&d) != cmOK) return 0;
    *line = atoi(buf);
    return 1;
}

int dlg_locate(char *name, int cap)
{
    return inputbox(" Locate Function ", "~F~unction Name", name, cap, "Enter name of function to locate");
}

void dlg_about(void)
{
    Dialog d; dlg_init(&d, " About ");
    d.w = 46; d.h = 13;
    dlg_static(&d, 15, 2, "Flash C");
    dlg_static(&d, 12, 3, "Version " FC_VERSION);
    dlg_static(&d, 4, 5, "A C compiler and text-mode IDE for Linux");
    dlg_static(&d, 4, 6, "native x86-64 code, no external tools");
    dlg_static(&d, 9, 8, "Press Alt-F1 for the help index");
    dlg_button(&d, 17, 10, 12, "  OK  ", cmOK, 1, NULL);
    dlg_run(&d);
}

void dlg_compiler_info(int lines, int errors, int warnings)
{
    char cwd[512] = "."; if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    Win *w = win_active();
    long avail = 0;
    FILE *f = fopen("/proc/meminfo", "r");
    if (f) { char l[128]; while (fgets(l, sizeof l, f)) if (sscanf(l, "MemAvailable: %ld", &avail) == 1) break; fclose(f); }
    char buf[900];
    snprintf(buf, sizeof buf,
        "Current directory : %.60s\n"
        "Current file      : %.60s\n"
        "Available memory  : %ld KB\n\n"
        "Lines compiled    : %d\n"
        "Total warnings    : %d\n"
        "Total errors      : %d",
        cwd, w && w->kind == W_EDIT ? w->buf->name : "", avail, lines, warnings, errors);
    msgbox(" Information ", buf, MB_OK);
}

/* ---------- window list ---------- */
int dlg_window_list(int *action)
{
    static char *items[MAXWIN]; static char store[MAXWIN][120];
    Dialog d; dlg_init(&d, " Window List ");
    d.w = 52; d.h = 17;
    int n = 0, sel = 0;
    for (int i = nwins - 1; i >= 0; i--) {
        Win *w = wins[i];
        snprintf(store[n], sizeof store[n], "%c %s", w->num ? '0' + w->num : ' ', w->title[0] ? w->title : "(untitled)");
        items[n] = store[n]; n++;
    }
    DItem *l = dlg_list(&d, 3, 2, 44, 10, items, n, "Use cursor keys to examine windows in windows list box");
    l->sel = sel;
    dlg_button(&d, 4, 13, 10, "~O~k", cmOK, 1, NULL);
    dlg_button(&d, 16, 13, 12, "~C~lose", cmDelete, 0, "Remove the selected entry from the windows list box");
    dlg_button(&d, 30, 13, 10, "Cancel", cmCancel, 0, NULL);
    int r = dlg_run(&d);
    *action = r == cmDelete;
    if (r != cmOK && r != cmDelete) return -1;
    return nwins - 1 - d.it[0].sel;
}

/* ---------- evaluate / modify ---------- */
static char ev_expr[256], ev_result[256], ev_new[256];
static void ev_draw(Dialog *d, DItem *it, int foc)
{
    scr_fill(d->x + it->x, d->y + it->y, it->w, 1, ' ', pal[P_DLG_INPUT]);
    scr_strn(d->x + it->x, d->y + it->y, ev_result, it->w, pal[P_DLG_INPUT]);
}
static int ev_cmd(Dialog *d, int cmd)
{
    if (cmd == cmEvaluate || cmd == cmOK) {
        long long v; char err[160];
        if (!ev_expr[0]) return 1;
        if (!fc_eval_expr(ev_expr, &v, err, sizeof err)) {
            snprintf(ev_result, sizeof ev_result, "%lld", v);
            if (v >= 0 && v < 0x100000000LL) { char hx[40]; snprintf(hx, sizeof hx, "  (0x%llX)", v); strcat(ev_result, hx); }
            if (v >= 32 && v < 127) { char c[16]; snprintf(c, sizeof c, "  '%c'", (char)v); strcat(ev_result, c); }
        } else snprintf(ev_result, sizeof ev_result, "%s", err);
        return 1;
    }
    if (cmd == cmModify) { msgbox(" Evaluate and Modify ", "Variables can only be modified while a\nprogram is being debugged.", MB_OK); return 1; }
    return 0;
}
void dlg_eval(void)
{
    Dialog d; dlg_init(&d, " Evaluate and Modify ");
    d.w = 66; d.h = 16; d.on_cmd = ev_cmd;
    dlg_input(&d, 3, 1, 56, "E~x~pression", ev_expr, sizeof ev_expr, "Enter expression to evaluate");
    DItem lab = { .type = D_LABEL, .x = 3, .y = 4, .text = "~R~esult" };
    dlg_add(&d, lab);
    DItem res = { .type = D_CUSTOM, .x = 3, .y = 5, .w = 56, .h = 1, .draw = ev_draw };
    dlg_add(&d, res);
    dlg_input(&d, 3, 7, 56, "~N~ew Value", ev_new, sizeof ev_new, "Enter new value for expression");
    dlg_button(&d, 3, 11, 12, "~E~valuate", cmEvaluate, 1, "Evaluate expression");
    dlg_button(&d, 17, 11, 12, "~M~odify", cmModify, 0, "Modify expression");
    dlg_button(&d, 31, 11, 12, "Cancel", cmCancel, 0, NULL);
    dlg_button(&d, 45, 11, 12, "Help", cmHelp, 0, NULL);
    ev_result[0] = 0;
    dlg_run(&d);
}

/* ---------- transfer items ---------- */
static void xfer_edit(TransferItem *t)
{
    Dialog d; dlg_init(&d, " Modify/New Transfer Item ");
    d.w = 64; d.h = 15;
    dlg_input(&d, 3, 1, 56, "Program ~T~itle", t->title, sizeof t->title, "Enter title you want assigned to the Transfer item (use ~x~ for the hot key)");
    dlg_input(&d, 3, 4, 56, "Program ~P~ath", t->path, sizeof t->path, "Enter the path and program name associated with this item");
    dlg_input(&d, 3, 7, 56, "~C~ommand Line", t->cmd, sizeof t->cmd, "Enter the parameterized command line ($NAME, $EDNAME, $PROMPT)");
    dlg_button(&d, 16, 11, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 30, 11, 10, "Cancel", cmCancel, 0, NULL);
    dlg_run(&d);
}

void dlg_transfer(void)
{
    static char *items[12]; static char store[12][80];
    for (;;) {
        Dialog d; dlg_init(&d, " Transfer ");
        d.w = 60; d.h = 17;
        for (int i = 0; i < cfg.nxfer; i++) {
            int k = 0; for (const char *p = cfg.xfer[i].title; *p && k < 78; p++) if (*p != '~') store[i][k++] = *p;
            store[i][k] = 0; items[i] = store[i];
        }
        DItem *l = dlg_list(&d, 3, 3, 38, 8, items, cfg.nxfer, "Use cursor keys to examine the list of Transfer items");
        DItem lab = { .type = D_LABEL, .x = 3, .y = 2, .text = "Program ~T~itles" };
        dlg_add(&d, lab); l->link = d.n - 1;
        dlg_button(&d, 42, 3, 14, "~E~dit", cmEdit, 0, "View and modify the description of the selected Transfer item");
        dlg_button(&d, 42, 5, 14, "~D~elete", cmDelete, 0, "Delete the selected Transfer item");
        dlg_button(&d, 42, 7, 14, "~N~ew", cmNew, 0, "Create a new Transfer item");
        dlg_button(&d, 42, 9, 14, "OK", cmOK, 1, NULL);
        dlg_button(&d, 42, 11, 14, "Cancel", cmCancel, 0, NULL);
        int r = dlg_run(&d), sel = l->sel;
        if (r == cmEdit && cfg.nxfer) xfer_edit(&cfg.xfer[sel]);
        else if (r == cmDelete && cfg.nxfer) { memmove(&cfg.xfer[sel], &cfg.xfer[sel + 1], (cfg.nxfer - sel - 1) * sizeof(TransferItem)); cfg.nxfer--; }
        else if (r == cmNew && cfg.nxfer < 12) { TransferItem *t = &cfg.xfer[cfg.nxfer]; memset(t, 0, sizeof *t); xfer_edit(t); if (t->title[0]) cfg.nxfer++; }
        else break;
    }
}

/* ---------- colors ---------- */
static int col_group, col_item, col_fg, col_bg;
static char *grp_names[P_COUNT]; static int ngrp;
static char *item_names[P_COUNT]; static int item_idx[P_COUNT]; static int nitem;

static void col_fill_items(void)
{
    nitem = 0;
    for (int i = 0; i < P_COUNT; i++) if (!strcmp(palinfo[i].group, grp_names[col_group])) { item_names[nitem] = (char *)palinfo[i].item; item_idx[nitem++] = i; }
}
static void col_sync(Dialog *d)
{
    DItem *gl = &d->it[0], *il = &d->it[1];
    if (gl->sel != col_group) { col_group = gl->sel; col_fill_items(); il->items = item_names; il->nitems = nitem; il->sel = il->top = 0; }
    col_item = il->sel;
    int idx = item_idx[col_item];
    col_fg = pal[idx] & 15; col_bg = (pal[idx] >> 4) & 7;
}
static void col_change(Dialog *d) { col_sync(d); }
static void col_draw_fg(Dialog *d, DItem *it, int foc)
{
    for (int i = 0; i < 16; i++) {
        uint8_t a = (i << 0) | (i == 0 ? 0x70 : 0);
        scr_put(d->x + it->x + i * 2, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        scr_put(d->x + it->x + i * 2 + 1, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        if (i == col_fg) { scr_put(d->x + it->x + i * 2, d->y + it->y + 1, 0x25B2, pal[P_DLG_NORM]); }
        (void)a;
    }
    if (foc) scr_cursor(d->x + it->x + col_fg * 2, d->y + it->y + 1, 1);
}
static void col_draw_bg(Dialog *d, DItem *it, int foc)
{
    for (int i = 0; i < 8; i++) {
        scr_put(d->x + it->x + i * 2, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        scr_put(d->x + it->x + i * 2 + 1, d->y + it->y, 0x2588, i == 0 ? 0x07 : i);
        if (i == col_bg) scr_put(d->x + it->x + i * 2, d->y + it->y + 1, 0x25B2, pal[P_DLG_NORM]);
    }
    if (foc) scr_cursor(d->x + it->x + col_bg * 2, d->y + it->y + 1, 1);
}
static void col_apply(void) { pal[item_idx[col_item]] = (col_bg << 4) | col_fg; }
static int col_key_fg(Dialog *d, DItem *it, int key)
{
    int b = key & KMASK;
    if (b == KEY_LEFT && col_fg > 0) col_fg--; else if (b == KEY_RIGHT && col_fg < 15) col_fg++; else return 0;
    col_apply(); return 1;
}
static int col_key_bg(Dialog *d, DItem *it, int key)
{
    int b = key & KMASK;
    if (b == KEY_LEFT && col_bg > 0) col_bg--; else if (b == KEY_RIGHT && col_bg < 7) col_bg++; else return 0;
    col_apply(); return 1;
}
static void col_draw_sample(Dialog *d, DItem *it, int foc)
{
    uint8_t a = pal[item_idx[col_item]];
    scr_fill(d->x + it->x, d->y + it->y, it->w, 3, ' ', a);
    scr_str(d->x + it->x + 2, d->y + it->y + 1, "Text", a);
}

void dlg_colors(void)
{
    ngrp = 0;
    for (int i = 0; i < P_COUNT; i++) {
        int seen = 0;
        for (int k = 0; k < ngrp; k++) if (!strcmp(grp_names[k], palinfo[i].group)) seen = 1;
        if (!seen) grp_names[ngrp++] = (char *)palinfo[i].group;
    }
    col_group = 0; col_fill_items();
    uint8_t bak[P_COUNT]; memcpy(bak, pal, sizeof pal);
    Dialog d; dlg_init(&d, " Colors ");
    d.w = 66; d.h = 20; d.on_change = col_change;
    dlg_list(&d, 3, 3, 24, 10, grp_names, ngrp, "Chooses general color group to modify");
    dlg_list(&d, 30, 3, 24, 10, item_names, nitem, "Chooses specific item in color group to modify");
    DItem l1 = { .type = D_LABEL, .x = 3, .y = 2, .text = "~G~roup" }; dlg_add(&d, l1); d.it[0].link = d.n - 1;
    DItem l2 = { .type = D_LABEL, .x = 30, .y = 2, .text = "~I~tem" }; dlg_add(&d, l2); d.it[1].link = d.n - 1;
    dlg_static(&d, 3, 14, "~F~oreground");
    DItem fg = { .type = D_CUSTOM, .x = 3, .y = 15, .w = 32, .h = 2, .draw = col_draw_fg, .onkey = col_key_fg, .hint = "Sets the foreground color for selected item (left / right)" }; dlg_add(&d, fg);
    dlg_static(&d, 38, 14, "~B~ackground");
    DItem bg = { .type = D_CUSTOM, .x = 38, .y = 15, .w = 16, .h = 2, .draw = col_draw_bg, .onkey = col_key_bg, .hint = "Sets the background color for selected item (left / right)" }; dlg_add(&d, bg);
    DItem sm = { .type = D_CUSTOM, .x = 56, .y = 14, .w = 8, .h = 3, .draw = col_draw_sample }; dlg_add(&d, sm);
    dlg_button(&d, 56, 3, 8, "OK", cmOK, 1, NULL);
    dlg_button(&d, 56, 6, 8, "Cancel", cmCancel, 0, NULL);
    col_sync(&d);
    int r = dlg_run(&d);
    if (r != cmOK) memcpy(pal, bak, sizeof pal);
    else memcpy(cfg.colors, pal, sizeof pal);
}

/* ---------- project item dialogs ---------- */
void dlg_local_options(const char *file)
{
    static char opts[200], outp[200]; static int ovl, exdbg, exlink;
    Dialog d; dlg_init(&d, " Local Options ");
    d.w = 64; d.h = 18;
    char t[300]; snprintf(t, sizeof t, "Project Item: %s", file);
    dlg_static(&d, 3, 1, t);
    dlg_input(&d, 3, 3, 56, "~C~ommand Line Options", opts, sizeof opts, "Command-line parameters to pass to the item's translator");
    dlg_input(&d, 3, 6, 56, "Output ~P~ath", outp, sizeof outp, "The file generated by translating this item");
    dlg_check(&d, 3, 9, "~O~verlay this module", &ovl, 0, "Chooses whether this project item should be overlaid");
    dlg_check(&d, 3, 10, "~E~xclude debug information", &exdbg, 0, "Chooses whether debug information should be generated for this item");
    dlg_check(&d, 3, 11, "Exclude from ~l~ink", &exlink, 0, "Chooses whether this item should be linked into the program");
    dlg_button(&d, 16, 14, 10, "OK", cmOK, 1, NULL);
    dlg_button(&d, 30, 14, 10, "Cancel", cmCancel, 0, NULL);
    dlg_run(&d);
}

void dlg_include_files(const char *file)
{
    static char *items[200]; static char store[200][160];
    int n = 0;
    FILE *f = fopen(file, "r");
    if (f) {
        char line[512];
        while (n < 200 && fgets(line, sizeof line, f)) {
            char *p = line; while (*p == ' ' || *p == '\t') p++;
            if (*p != '#') continue;
            p++; while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "include", 7)) continue;
            p += 7; while (*p == ' ' || *p == '\t') p++;
            char close = *p == '<' ? '>' : '"';
            if (*p != '<' && *p != '"') continue;
            char *e = strchr(p + 1, close); if (!e) continue;
            snprintf(store[n], sizeof store[n], "%.*s", (int)(e - p - 1), p + 1);
            items[n] = store[n]; n++;
        }
        fclose(f);
    }
    Dialog d; dlg_init(&d, " Include Files ");
    d.w = 56; d.h = 17;
    char t[300]; snprintf(t, sizeof t, "Include files for %s", file);
    dlg_static(&d, 3, 1, t);
    if (!n) { items[0] = "(none)"; n = 1; }
    dlg_list(&d, 3, 3, 36, 9, items, n, "Use cursor keys to examine list of dependent (include) files");
    dlg_button(&d, 42, 3, 10, "~V~iew", cmView, 1, "Load the selected include file into an Edit window for viewing");
    dlg_button(&d, 42, 5, 10, "Cancel", cmCancel, 0, NULL);
    int r = dlg_run(&d);
    if (r == cmView && strcmp(items[d.it[1].sel], "(none)")) {
        char path[600]; const char *sl = strrchr(file, '/');
        if (sl) snprintf(path, sizeof path, "%.*s/%s", (int)(sl - file), file, items[d.it[1].sel]); else snprintf(path, sizeof path, "%s", items[d.it[1].sel]);
        if (file_exists(path)) win_open_file(path);
        else infobox(" Include Files ", "%s is not in the source directory", items[d.it[1].sel]);
    }
}
