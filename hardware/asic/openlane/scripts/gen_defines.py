#!/usr/bin/env python3

import argparse


def parse_arguments():
    help_str = """
Example usage:
    ./gen_defines.py -o defines.vh DATA_W=32 ADDR_W=20 USE_RAM

Generates:
    // defines.vh file
    `define DATA_W 32
    `define DATA_W 20
    `define USE_RAM
"""
    parser = argparse.ArgumentParser(
        description="Generate verilog header file from input argument defines.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=help_str,
    )
    parser.add_argument(
        "-o", "--output", default="stdout", help="Output file. Stdout by default."
    )
    parser.add_argument(
        "defines", nargs="*", help="List of defines: MACRO or MACRO=value."
    )
    return parser.parse_args()


def format_defines(defines):
    formated_str = "// file generated by gen_defines.py\n"
    for define in defines:
        formated_str = f'{formated_str}`define {define.replace("=", " ")}\n'
    return formated_str


def output_defines(fout, formated_defines):
    if fout == "stdout":
        print(formated_defines)
    else:
        with open(fout, "w") as f:
            f.write(formated_defines)


if __name__ == "__main__":
    args = parse_arguments()
    output_defines(args.output, format_defines(args.defines))
