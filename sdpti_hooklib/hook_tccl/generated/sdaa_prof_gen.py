#!/usr/bin/python

import os, sys, re
import CppHeaderParser
import filecmp

REC_MAX_LEN = 4096

# Messages and errors controll
verbose = 0
errexit = 0
inp_file = "none"
line_num = -1

# Verbose message
def message(msg):
    if verbose:
        sys.stdout.write(msg + "\n")


# Fatal error termination
def error(msg):
    if line_num != -1:
        msg += ", file '" + inp_file + "', line (" + str(line_num) + ")"
    if errexit:
        msg = " Error: " + msg
    else:
        msg = " Warning: " + msg

    sys.stdout.write(msg + "\n")
    sys.stderr.write(sys.argv[0] + msg + "\n")


def fatal(msg):
    error(msg)
    sys.exit(1)


# Normalizing API name
def filtr_api_name(name):
    name = re.sub(r"\s*$", r"", name)
    return name


def filtr_api_decl(record):
    record = re.sub("\s__dparm\([^\)]*\)", r"", record)
    record = re.sub("\(void\*\)", r"", record)
    return record


# Normalizing API arguments
def filtr_api_args(args_str):
    args_str = re.sub(r"^\s*", r"", args_str)
    args_str = re.sub(r"\s*$", r"", args_str)
    args_str = re.sub(r"\s*,\s*", r",", args_str)
    args_str = re.sub(r"\s+", r" ", args_str)
    args_str = re.sub(r"\s*(\*+)\s*", r"\1 ", args_str)
    args_str = re.sub(r"(\benum|struct) ", "", args_str)
    return args_str


# Normalizing types
def norm_api_types(type_str):
    type_str = re.sub(r"uint32_t", r"unsigned int", type_str)
    type_str = re.sub(r"^unsigned$", r"unsigned int", type_str)
    return type_str


# Creating a list of arguments [(type, name), ...]
def list_api_args(args_str):
    args_str = filtr_api_args(args_str)
    args_list = []
    if args_str != "":
        for arg_pair in args_str.split(","):
            if arg_pair == "void":
                continue
            arg_pair = re.sub(r"\s*=\s*\S+$", "", arg_pair)
            m = re.match("^(.*)\s(\S+)$", arg_pair)
            if m:
                arg_type = norm_api_types(m.group(1))
                arg_name = m.group(2)
                args_list.append((arg_type, arg_name))
            else:
                fatal(
                    "bad args: args_str: '"
                    + args_str
                    + "' arg_pair: '"
                    + arg_pair
                    + "'"
                )
    return args_list


# Creating arguments string "type0, type1, ..."
def filtr_api_types(args_str):
    args_list = list_api_args(args_str)
    types_str = ""
    for arg_tuple in args_list:
        types_str += arg_tuple[0] + ", "
    return types_str


# Checking for pointer non-void arg type
def pointer_ck(arg_type):
    ptr_type = ""
    m = re.match(r"(.*)\*$", arg_type)
    if m:
        ptr_type = m.group(1)
        n = re.match(r"(.*)\*\*$", arg_type)
        if not n:
            ptr_type = re.sub(r"const ", "", ptr_type)
        if ptr_type == "void":
            ptr_type = ""
    return ptr_type


# Creating options list [opt0, opt1, ...]
def filtr_api_opts(args_str):
    args_list = list_api_args(args_str)
    opts_list = []
    for arg_tuple in args_list:
        opts_list.append(arg_tuple[1])
    return opts_list


# main
# API header fiile xx.h
if len(sys.argv) < 5:
    print(
        "Usage: "
        + sys.argv[0]
        + " <tccl.h file path> <init.cc file path> <src dir(collectives) path> <previous output cbid.h> \n"
        + "\n"
        + "  Example:\n"
        + "  $ python3 "
        + sys.argv[0]
        + " ../../../src/tccl.h"
        + " ../../../src/init.cc ../../../src/collectives ./sdpti_tccl_cbid.h"
    )
    sys.exit(0)

api_hfile = sys.argv[1]
print(api_hfile)

if not os.path.isfile(api_hfile):
    fatal("input file '" + api_hfile + "' not found")


# Srcs directory given as an argument
src_file = sys.argv[2]
print(src_file)
if not os.path.isfile(src_file):
    fatal("src file " + src_file + "' not found")


# Current sdpti_tccl_cbid.h include
src_dir = sys.argv[3]
print(src_dir)
if not os.path.isdir(src_dir):
    fatal("src dir '" + src_dir + "' not found")

src_pat = "\.cc$"

INPUT = sys.argv[4]
print(INPUT)
if not os.path.isfile(INPUT):
    fatal("input file '" + INPUT + "' not found")

def parse_api(input_file_t, out):
    global intput_file
    global line_num
    intput_file = input_file_t

    beg_pattern = re.compile(r"^(tcclResult_t|const\s+char\s*\*\s*)\s*([^\(]+)\(")
    api_pattern = re.compile(r"^(tcclResult_t|const\s+char\s*\*\s*)\s*([^\(]+)\(([^\)]*)\)")

    inp = open(intput_file, "r")

    found = 0
    record = ""
    line_num = -1

    for line in inp.readlines():
        record += re.sub(r"^\s+", r" ", line[:-1])
        line_num += 1

        m = beg_pattern.match(line)
        if m:
            name = m.group(2)
            message("api: " + name)
            found = 1

        if found != 0:
            record = re.sub("\s__dparm\([^\)]*\)", "", record)
            m = api_pattern.match(record)
            if m:
                found = 0
                api_name = filtr_api_name(m.group(2))
                api_args = m.group(3)
                if not api_name in out:
                    out[api_name] = api_args
            else:
                continue
        record = ""

    inp.close()

def parse_content(input_file, api_map, out):

    # open inout file
    inp = open(input_file, "r")

    # API method begin pattern
    beg_pattern = re.compile("^(tcclResult_t|const char\s*\*)\s+[^\(]+\(")

    # API declaration pattern
    decl_pattern = re.compile("^(tcclResult_t|const char\s*\*)\s+([^\(]+)\(([^\)]*)\)\s*;")

    # API definition pattern
    api_pattern = re.compile("^(tcclResult_t|const char\s*\*)\s+([^\(]+)\(([^\)]*)\)\s*{")


    # API name
    api_name = ""
    # Valid public API found flag
    api_valid = 0

    # Input file patched content
    content = ""

    # API beginning found flag
    found = 0

    # Current record, accumulating several API definition related lines
    record = ""
    # Current input file line number
    line_num = -1

    # Reading input file
    for line in inp.readlines():
        # Accumulating record
        record += re.sub(r"^\s+", r" ", line[:-1])
        line_num += 1

        if found == 0:
            if beg_pattern.match(record):
                found = 1
                record = filtr_api_decl(record)

        # Matching API declaration
        if found == 1:
            if decl_pattern.match(record):
                found = 0
                print("Should not here")

        if found == 1:
            m = api_pattern.match(record)
            if m:
                found = 2
                api_valid = 0
                api_overload = 0

                api_name = filtr_api_name(m.group(2))

                if api_name in api_map:
                    # Getting API arguments
                    api_args = m.group(3)

                    # Getting etalon arguments from the API map
                    eta_args = api_map[api_name]

                    if eta_args != api_args:
                        fatal(".h and .cc Not Same")
                    if eta_args == "":
                        eta_args = api_args
                        api_map[api_name] = eta_args
                    # Normalizing API arguments
                    api_types = filtr_api_types(api_args)

                    # Normalizing etalon arguments
                    eta_types = filtr_api_types(eta_args)

                    if api_types == eta_types:
                        if api_name in out:
                            fatal(
                                'API redefined "'
                                + api_name
                                + '", record "'
                                + record
                                + '"'
                            )

                        api_valid = 1

                        out[api_name] = filtr_api_opts(api_args)
                    else:
                        fatal("define and implementaion Not Same")

        if found == 2:
            if re.search("return", line):
             found = 0
            if api_valid == 1:
                message("\t" + api_name)


        if found != 1:
            record = ""
        content += line

    inp.close()
    line_num = -1

    if len(out) != 0:
        return content
    else:
        return ""


def gen_info(f):
    f.write("// Generated file. DO NOT EDIT.\n")
    f.write("//\n")
    f.write(
        "// This file is automatically generated by the "
        + os.path.basename(__file__)
        + " script.\n"
    )
    f.write(
        "// If changes are required, run the script and commit the updated file.\n\n"
    )


def generate_cbid_header(f, api_map, callback_ids, opts_map):
    # Private API list
    priv_lst = []
    gen_info(f)
    # Generating the callbacks ID enumaration
    f.write("typedef enum SDpti_tccl_api_trace_cbid_enum {\n")
    f.write("  SDPTI_TCCL_TRACE_CBID_INVALID = 0,\n")

    cb_id_map = {}
    last_cb_id = 0
    for name, cb_id in callback_ids:
        f.write("  SDPTI_TCCL_TRACE_CBID_" + name + " = " + str(cb_id) + ",\n")
        cb_id_map[name] = cb_id
        if cb_id > last_cb_id:
            last_cb_id = cb_id

    for name in api_map.keys():
        if not name in cb_id_map:
            last_cb_id += 1
            f.write(
                "  SDPTI_TCCL_TRACE_CBID_" + name + " = " + str(last_cb_id) + ",\n"
            )

    f.write("  SDPTI_TCCL_TRACE_CBID_SIZE = " + str(last_cb_id + 1) + ",\n")
    f.write("  SDPTI_TCCL_TRACE_CBID_FORCE_INT = " + "0x7fffffff" + "\n")
    f.write("} SDpti_tccl_api_trace_cbid;\n")


def generate_meta_header(f, api_map, callback_ids, opts_map):
    # Generating the callbacks data structure
    gen_info(f)
    f.write('#include "tccl.h"\n')
    f.write("\n")
    
    # if const tcclComm_t --> cosnt struct tcclComm*
    const_tcclComm_t_pattern = re.compile(r'\bconst\s+tcclComm_t\b')

    for name in api_map.keys():
        args = api_map[name]
        if len(args) != 0:
            f.write("typedef struct " + name + "_params_st {" + "\n")
            for arg_tuple in args:
                arg_type = arg_tuple[0]
                ptr_type = pointer_ck(arg_type)
                arg_name = arg_tuple[1]

                if const_tcclComm_t_pattern.match(arg_type):
                    arg_type = "const struct tcclComm*"

                # if int[], size_t[] --> int* size_t*
                if '[]' in arg_name:
                    arg_name = arg_name.replace('[]', '')  # remove '[]' in arg_name
                    arg_type += '*'  # remove '*' in arg_type

                # Structuer field code
                f.write("  " + arg_type + " " + arg_name + ";\n")
                # if ptr_type != "":
                #     f.write("  " + ptr_type + " " + arg_name + "__val;\n")
            f.write("} " + name + "_params" + ";\n\n")


def generate_tccl_hook_macro(f, api_map, callback_ids, opts_map):
    gen_info(f)
    f.write("#ifndef __SDPTI_HOOK_TCCL_MACRO_H__\n")
    f.write("#define __SDPTI_HOOK_TCCL_MACRO_H__\n")
    f.write("\n// sdpti hook api marco\n")
    for name in api_map.keys():
        f.write("#define " + name + " " + "sdpti_hook_" + name + "\n")
    f.write("\n#endif /* __SDPTI_HOOK_TCCL_MACRO_H__ */")

def generate_tccl_private_src(f, api_map, callback_ids, opts_map):
    gen_info(f)
    f.write('#include "sdpti_hook_tccl.h"\n\n')

    for name in api_map.keys():
        args = api_map[name]
        if name in ['tcclGetLastError','tcclGetErrorString']:
            f.write("extern const char* " + "sdpti_hook_" + name + "(")
        else:
            f.write("extern tcclResult_t " + "sdpti_hook_" + name + "(")
        for ind in range(0, len(args)):
            arg_tuple = args[ind]
            arg_type = arg_tuple[0]
            fld_name = arg_tuple[1]
            f.write(arg_type + " " + fld_name)
            if ind != (len(args) - 1):
                f.write(", ")
        f.write(");\n")
    f.write("\n")

    for name in api_map.keys():
        args = api_map[name]
        user_args = ""
        if name in ['tcclGetLastError','tcclGetErrorString']:
            f.write("const char* " + name + "(")
        else:
            f.write("tcclResult_t " + name + "(")
        for ind in range(0, len(args)):
            arg_tuple = args[ind]
            arg_type = arg_tuple[0]
            fld_name = arg_tuple[1]
            f.write(arg_type + " " + fld_name)
            user_args += fld_name
            if ind != (len(args) - 1):
                f.write(", ")
                user_args += ", "
        f.write(")\n{\n")
        f.write("  " + "SD_CB_ENTER(" + name + ");\n")
        modified_user_args = user_args
        for arg_tuple in args:
            fld_name = arg_tuple[1]
            if '[]' in fld_name:
                modified_user_args = modified_user_args.replace(fld_name, fld_name.replace('[]', ''))
        f.write(
            "  return "
            + "SD_CB_EXIT("
            + "sdpti_hook_"
            + name
            + "("
            + modified_user_args
            + "));\n"
        )
        f.write("}\n\n")


def generate_tccl_private_util_src(f, api_map, callback_ids, opts_map):
    gen_info(f)
    f.write('#include "sdpti_hook_tccl_internal.h"\n')

    f.write("const char* tccl_cbid_to_str(SDpti_tccl_api_trace_cbid cbid) {\n")
    f.write("  switch(cbid) {\n")
    for name, cb_id in callback_ids:
        f.write("    case SDPTI_TCCL_TRACE_CBID_" + name + ":\n")
        f.write("      return \"" + name + "\";\n")
    
    f.write("    default:\n")
    f.write("      return \"unknown\";\n")
    f.write("  }\n")
    f.write("}\n")

    f.write("SDpti_tccl_api_trace_cbid tccl_str_to_cbid(const char* name) {\n")
    for name, cb_id in callback_ids:
        f.write("  if (strcmp(\"" + name + "\", name) == 0)\n")
        f.write("    return SDPTI_TCCL_TRACE_CBID_" + name + ";\n")
    f.write("  return SDPTI_TCCL_TRACE_CBID_INVALID;\n")
    f.write("}\n")

def generate_tccl_private_header(f, api_map, callback_ids, opts_map):
    # Generating the callbacks data structure
    gen_info(f)
    f.write("#ifndef __SDPTI_HOOK_TCCL_INTERNAL_H__\n")
    f.write("#define __SDPTI_HOOK_TCCL_INTERNAL_H__\n\n")
    f.write("#ifdef __cplusplus\n"
          + "extern \"C\" {\n"
          + "#endif /* __cplusplus */\n")
    f.write('#include "generated_tccl_api_meta.h"\n')
    f.write('#include "sdpti_tccl_cbid.h"\n')
    f.write("#include <string.h>\n")

    f.write("extern const char* tccl_cbid_to_str(SDpti_tccl_api_trace_cbid cbid);\n")
    f.write("extern SDpti_tccl_api_trace_cbid tccl_str_to_cbid(const char* name);\n")

    f.write("\n// TCCL API Hook data structures\n")
    f.write(
        "typedef struct tccl_api_data_t {\n"
        + "  uint64_t correlation_id;\n"
        + "  uint32_t phase;\n"
        + "  uint64_t *phase_data;\n"
        + "  union {\n"
    )

    # if const tcclComm_t --> cosnt struct tcclComm*
    const_tcclComm_t_pattern = re.compile(r'\bconst\s+tcclComm_t\b')

    for name in api_map.keys():
        f.write("    " + name + "_params " + name + ";\n")
    f.write("  } args;\n"  + "} tccl_api_data_t;\n")

    f.write("\n// TCCL API Hook data  filling macros\n")
    for name in api_map.keys():
        args = api_map[name]
        f.write("// " + name + str(args) + "\n")
        f.write("#define INIT_" + name + "_CB_ARGS_DATA(cb_data) { \\\n")
        # API args iterating:
        #   type is args[<ind>][0]
        #   name is args[<ind>][1]
        for ind in range(0, len(args)):
            arg_tuple = args[ind]
            arg_type = arg_tuple[0]
            ptr_type = pointer_ck(arg_type)
            fld_name = arg_tuple[1]
            opt_name = arg_tuple[1]
            if '[]' in fld_name:
                fld_name = fld_name.replace('[]', '')
                f.write(
                    "  cb_data.args." + name + "." + fld_name +
                    " = (" + arg_type + "*)" + fld_name + "; \\\n"
                )
            else:
                if arg_type == "const char*":
                    f.write(
                        "  cb_data.args."
                        + name
                        + "."
                        + fld_name
                        + " = ("
                        + opt_name
                        + ") ? strdup("
                        + opt_name
                        + ") : NULL; \\\n"
                    )
                else:
                    if const_tcclComm_t_pattern.match(arg_type):
                        arg_type = "const struct tcclComm*"
                    f.write(
                        "  cb_data.args."
                        + name
                        + "."
                        + fld_name
                        + " = ("
                        + arg_type
                        + ")"
                        + opt_name
                        + "; \\\n"
                    )
                
        f.write("};\n")
    f.write(
        "#define INIT_CB_ARGS_DATA(cb_id, cb_data) INIT_##cb_id##_CB_ARGS_DATA(cb_data)\n"
    )
    f.write("#ifdef __cplusplus\n"
          + "}\n"
          + "#endif /* __cplusplus */\n")
    f.write("\n#endif /* __SDPTI_HOOK_TCCL_INTERNAL_H__ */")

# src path walk
def parse_src(api_map,src_file ,src_path, src_patt, out):

    parse_content(src_file, api_map, out)
    pattern = re.compile(src_patt)
    src_path = re.sub(r"\s", "", src_path)
    for src_dir in src_path.split(":"):
        message("Parsing " + src_dir + " for '" + src_patt + "'")
        for root, dirs, files in os.walk(src_dir):
            for fnm in files:
                if pattern.search(fnm):
                    file = root + "/" + fnm
                    message(file)
                    content = parse_content(file, api_map, out)

# API declaration map
api_map = {}

# API options map
opts_map = {}

parse_api(api_hfile, api_map)

for key, value in api_map.items():
    message(f"Key: {key}, Value: {value}")

# Parsing sources
parse_src(api_map, src_file, src_dir, src_pat, opts_map)

for key, value in opts_map.items():
    message(f"Key: {key}, Value: {value}")

# Converting api map to map of lists
# Checking for not found APIs

for name in api_map.keys():
    args_str = api_map[name]
    api_map[name] = list_api_args(args_str)


for key, value in api_map.items():
    message(f"Key: {key}, Value: {value}")

try:
    cppHeader = CppHeaderParser.CppHeader(INPUT)
except CppHeaderParser.CppParseError as e:
    print(e)
    sys.exit(1)

# Callback IDs
api_callback_ids = []

for enum in cppHeader.enums:
    if enum["name"] == "SDpti_tccl_api_trace_cbid":
        for value in enum["values"]:
            if value["name"] == "SDPTI_TCCL_TRACE_CBID_INVALID":
                continue
            if value["name"] == "SDPTI_TCCL_TRACE_CBID_FORCE_INT":
                break
            m = re.match(r"SDPTI_TCCL_TRACE_CBID_(\S*)", value["name"])
            if m:
                if m.group(1) == "SIZE":
                    continue
                api_callback_ids.append((m.group(1), value["value"]))

CBID_FILE = "sdpti_tccl_cbid.h"
META_DATA = "generated_tccl_api_meta.h"
HOOK_TCCL = "sdpti_hook_tccl_internal.h"
HOOK_TCCL_SRC = "sdpti_hook_tccl_internal.c"
HOOK_TCCL_MACRO = "sdpti_hook_tccl_macro.h"
HOOK_TCCL_UTIL_SRC = "sdpti_hook_tccl_util.c"

with open(CBID_FILE, "w") as f:
    generate_cbid_header(f, api_map, api_callback_ids, opts_map)

with open(META_DATA, "w") as f:
    generate_meta_header(f, api_map, api_callback_ids, opts_map)

with open(HOOK_TCCL, "w") as f:
    generate_tccl_private_header(f, api_map, api_callback_ids, opts_map)

with open(HOOK_TCCL_SRC, "w") as f:
    generate_tccl_private_src(f, api_map, api_callback_ids, opts_map)

with open(HOOK_TCCL_MACRO, "w") as f:
    generate_tccl_hook_macro(f, api_map, api_callback_ids, opts_map)

with open(HOOK_TCCL_UTIL_SRC, "w") as f:
    generate_tccl_private_util_src(f, api_map, api_callback_ids, opts_map)

# python3 sdaa_prof_gen.py ../../../src/tccl.h ../../../src/init.cc ../../../src/collectives ./sdpti_tccl_cbid.h