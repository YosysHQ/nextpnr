/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
 *  Copyright (C) 2018  David Shah <dave@ds0.me>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */


#include "design.h"
#include "chip.h"
#include "emb.h"

// include after design.h/chip.h
#include "pybindings.h"

// Required to determine concatenated module name (which differs for different archs)
#define PASTER(x, y) x ## _ ## y
#define EVALUATOR(x, y)  PASTER(x,y)
#define MODULE_NAME EVALUATOR(nextpnrpy, ARCHNAME)
#define PYINIT_MODULE_NAME EVALUATOR(&PyInit_nextpnrpy, ARCHNAME)
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Architecture-specific bindings should be created in the below function, which must be implemented in all
// architectures
void arch_wrap_python();

BOOST_PYTHON_MODULE (MODULE_NAME) {
	arch_wrap_python();
}

void arch_appendinittab() {
	PyImport_AppendInittab(TOSTRING(MODULE_NAME), PYINIT_MODULE_NAME);
}

static wchar_t *program;

void init_python(const char *executable) {
	program = Py_DecodeLocale(executable, NULL);
	if (program == NULL) {
		fprintf(stderr, "Fatal error: cannot decode executable filename\n");
		exit(1);
	}
	try {
		PyImport_AppendInittab(TOSTRING(MODULE_NAME), PYINIT_MODULE_NAME);
		emb::append_inittab();
		Py_SetProgramName(program);
		Py_Initialize();
	} catch (boost::python::error_already_set const &) {
		// Parse and output the exception
		std::string perror_str = parse_python_exception();
		std::cout << "Error in Python: " << perror_str << std::endl;
	}
}

void deinit_python() {
	Py_Finalize();
	PyMem_RawFree(program);
}

void execute_python_file(const char *python_file) {
	try {
		FILE *fp = fopen(python_file, "r");
		if (fp == NULL) {
			fprintf(stderr, "Fatal error: file not found %s\n", python_file);
			exit(1);
		}
		PyRun_SimpleFile(fp, python_file);
		fclose(fp);
	}
	catch (boost::python::error_already_set const &) {
		// Parse and output the exception
		std::string perror_str = parse_python_exception();
		std::cout << "Error in Python: " << perror_str << std::endl;
	}
}

