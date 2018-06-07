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
#include "pybindings.h"
#include "emb.h"

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

bool operator==(const PortRef &a, const PortRef &b) {
	return (a.cell == b.cell) && (a.port == b.port);
}

BOOST_PYTHON_MODULE (MODULE_NAME) {
	class_<GraphicElement>("GraphicElement")
			.def_readwrite("style", &GraphicElement::style)
			.def_readwrite("type", &GraphicElement::type)
			.def_readwrite("x1", &GraphicElement::x1)
			.def_readwrite("y1", &GraphicElement::y1)
			.def_readwrite("x2", &GraphicElement::x2)
			.def_readwrite("y2", &GraphicElement::y2)
			.def_readwrite("text", &GraphicElement::text);

	class_<PortRef>("PortRef")
			.def_readwrite("cell", &PortRef::cell)
			.def_readwrite("port", &PortRef::port);

	class_<NetInfo, NetInfo*>("NetInfo")
			.def_readwrite("name", &NetInfo::name)
			.def_readwrite("driver", &NetInfo::driver)
			.def_readwrite("users", &NetInfo::users)
			.def_readwrite("attrs", &NetInfo::attrs)
			.def_readwrite("wires", &NetInfo::wires);

	WRAP_MAP(decltype(NetInfo::attrs), "IdStrMap");

	class_<vector<PortRef>>("PortRefVector")
			.def(vector_indexing_suite<vector<PortRef>>());

	class_<PortInfo>("PortInfo")
			.def_readwrite("name", &PortInfo::name)
			.def_readwrite("net", &PortInfo::net)
			.def_readwrite("type", &PortInfo::type);

	class_<CellInfo, CellInfo*>("CellInfo")
			.def_readwrite("name", &CellInfo::name)
			.def_readwrite("type", &CellInfo::type)
			.def_readwrite("ports", &CellInfo::ports)
			.def_readwrite("attrs", &CellInfo::attrs)
			.def_readwrite("params", &CellInfo::params)
			.def_readwrite("bel", &CellInfo::bel)
			.def_readwrite("pins", &CellInfo::pins);

	WRAP_MAP(decltype(CellInfo::ports), "IdPortMap");
	//WRAP_MAP(decltype(CellInfo::pins), "IdIdMap");

	class_<Design, Design*>("Design", no_init)
			.def_readwrite("chip", &Design::chip)
			.def_readwrite("nets", &Design::nets)
			.def_readwrite("cells", &Design::cells);

	WRAP_MAP(decltype(Design::nets), "IdNetMap");
	WRAP_MAP(decltype(Design::cells), "IdCellMap");

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
		PyImport_ImportModule(TOSTRING(MODULE_NAME));
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

