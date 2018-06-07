/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@clifford.at>
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
#include "mainwindow.h"
#include <QApplication>
#include <iostream>
#include <fstream>
#include "version.h"
#include <boost/program_options.hpp>
#include "pybindings.h"
#include "jsonparse.h"

void svg_dump_el(const GraphicElement &el)
{
	float scale = 10.0, offset = 10.0;
	std::string style = "stroke=\"black\" stroke-width=\"0.1\" fill=\"none\"";

	if (el.type == GraphicElement::G_BOX) {
		std::cout << "<rect x=\"" << (offset + scale*el.x1) << "\" y=\"" << (offset + scale*el.y1) <<
				"\" height=\"" << (scale*(el.y2-el.y1)) << "\" width=\"" << (scale*(el.x2-el.x1)) << "\" " << style << "/>\n";
	}

	if (el.type == GraphicElement::G_LINE) {
		std::cout << "<line x1=\"" << (offset + scale*el.x1) << "\" y1=\"" << (offset + scale*el.y1) <<
				"\" x2=\"" << (offset + scale*el.x2) << "\" y2=\"" << (offset + scale*el.y2) << "\" " << style << "/>\n";
	}
}

int main(int argc, char *argv[])
{
	namespace po = boost::program_options;
	int rc = 0;
	std::string str;

	po::options_description options("Allowed options");
	options.add_options()("help,h","show help");
	options.add_options()("test","just a check");
	options.add_options()("gui","start gui");
	options.add_options()("svg","dump SVG file");
	options.add_options()("file", po::value<std::vector<std::string>>(), "python file to execute");
	options.add_options()("json", po::value<std::string>(), "JSON design file to ingest");
	options.add_options()("version,v","show version");	
	options.add_options()("lp384","set device type to iCE40LP384");
	options.add_options()("lp1k","set device type to iCE40LP1K");
	options.add_options()("lp8k","set device type to iCE40LP8K");
	options.add_options()("hx1k","set device type to iCE40HX1K");
	options.add_options()("hx8k","set device type to iCE40HX8K");
	options.add_options()("up5k","set device type to iCE40UP5K");

	po::positional_options_description pos;
	pos.add("file", -1);

	po::variables_map vm;
	try {
		po::parsed_options parsed = po::command_line_parser(argc, argv).
		options(options).
		positional(pos).
		run();

		po::store(parsed, vm);
		
		po::notify(vm);   
	}

	catch(std::exception& e)
	{
		std::cout << e.what() << "\n";
		return 1;
	}

	if (vm.count("help") || argc == 1)
	{
		std::cout << basename(argv[0]) << " -- Next Generation Place and Route (git sha1 " GIT_COMMIT_HASH_STR ")\n";
		std::cout << "\n";
		std::cout << options << "\n";
		return 1;
	}
	
	if (vm.count("version"))
	{
		std::cout << basename(argv[0])
			<< " -- Next Generation Place and Route (git sha1 "
					GIT_COMMIT_HASH_STR ")\n";
		return 1;
	}

	ChipArgs chipArgs;
	chipArgs.type = ChipArgs::HX1K;

	if (vm.count("lp384"))
		chipArgs.type = ChipArgs::LP384;

	if (vm.count("lp1k"))
		chipArgs.type = ChipArgs::LP1K;

	if (vm.count("lp8k"))
		chipArgs.type = ChipArgs::LP8K;

	if (vm.count("hx1k"))
		chipArgs.type = ChipArgs::HX1K;

	if (vm.count("hx8k"))
		chipArgs.type = ChipArgs::HX8K;

	if (vm.count("up5k"))
		chipArgs.type = ChipArgs::UP5K;

	Design design(chipArgs);
	init_python(argv[0]);
	python_export_global("design", design);

	if (vm.count("test"))
	{
		int bel_count = 0, wire_count = 0, pip_count = 0;

		std::cout << "Checking bel names.\n";
		for (auto bel : design.chip.getBels()) {
			auto name = design.chip.getBelName(bel);
			assert(bel == design.chip.getBelByName(name));
			bel_count++;
		}
		std::cout << "  checked " << bel_count << " bels.\n";

		std::cout << "Checking wire names.\n";
		for (auto wire : design.chip.getWires()) {
			auto name = design.chip.getWireName(wire);
			assert(wire == design.chip.getWireByName(name));
			wire_count++;
		}
		std::cout << "  checked " << wire_count << " wires.\n";

		std::cout << "Checking pip names.\n";
		for (auto pip : design.chip.getPips()) {
			auto name = design.chip.getPipName(pip);
			assert(pip == design.chip.getPipByName(name));
			pip_count++;
		}
		std::cout << "  checked " << pip_count << " pips.\n";

		std::cout << "Checking uphill -> downhill consistency.\n";
		for (auto dst : design.chip.getWires()) {
			for (auto uphill_pip : design.chip.getPipsUphill(dst)) {
				bool found_downhill = false;
				for (auto downhill_pip : design.chip.getPipsDownhill(design.chip.getPipSrcWire(uphill_pip))) {
					if (uphill_pip == downhill_pip) {
						assert(!found_downhill);
						found_downhill = true;
					}
				}
				assert(found_downhill);
			}
		}

		std::cout << "Checking downhill -> uphill consistency.\n";
		for (auto dst : design.chip.getWires()) {
			for (auto downhill_pip : design.chip.getPipsDownhill(dst)) {
				bool found_uphill = false;
				for (auto uphill_pip : design.chip.getPipsUphill(design.chip.getPipDstWire(downhill_pip))) {
					if (uphill_pip == downhill_pip) {
						assert(!found_uphill);
						found_uphill = true;
					}
				}
				assert(found_uphill);
			}
		}

		return 0;
	}

	if (vm.count("svg"))
	{
		std::cout << "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";
		for (auto bel : design.chip.getBels()) {
			std::cout << "<!-- " << design.chip.getBelName(bel) << " -->\n";
			for (auto &el : design.chip.getBelGraphics(bel))
				svg_dump_el(el);
		}
		std::cout << "<!-- Frame -->\n";
		for (auto &el : design.chip.getFrameGraphics())
			svg_dump_el(el);
		std::cout << "</svg>\n";
	}

	if (vm.count("json")) 
	{
		std::string filename = vm["json"].as<std::string>();
		std::istream *f = new std::ifstream(filename);
		
		parse_json_file(f, filename, &design);
	}	

	if (vm.count("file")) 
	{
		std::vector<std::string> files = vm["file"].as<std::vector<std::string>>();
		for(auto filename : files)
			execute_python_file(filename.c_str());
	}

	if (vm.count("gui"))
	{
		QApplication a(argc, argv);
		MainWindow w;
		w.show();

		rc = a.exec();
	}
	deinit_python();
	return rc;
}
