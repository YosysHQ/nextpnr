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
#include "version.h"
#include <boost/program_options.hpp>
#include "pybindings.h"

int main(int argc, char *argv[])
{
	namespace po = boost::program_options;

	std::string str;

	po::options_description options("Allowed options");
	options.add_options()("help,h","show help");
	options.add_options()("debug","just a check");
	options.add_options()("gui","start gui");
	options.add_options()("file", po::value<std::string>(), "python file to execute");
	options.add_options()("version,v","show version");	

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
		std::cout << basename(argv[0]) << " -- Next Generation Place and Route (git sha1 " GIT_COMMIT_HASH_STR ")\n";
		return 1;
	}

	if (vm.count("gui")) 
	{
		QApplication a(argc, argv);
		MainWindow w;
		w.show();

		return a.exec();
	}

	if (vm.count("debug")) 
	{
		ChipArgs chipArgs;
		chipArgs.type = ChipArgs::LP384;

		Design design(chipArgs);		
		for (auto bel : design.chip.getBels())
			printf("%s\n", design.chip.getBelName(bel).c_str());
		return 0;
	}

	if (vm.count("file")) 
	{
		std::string filename = vm["file"].as<std::string>();
		execute_python_file(argv[0],filename.c_str());
    }	
	
	return 0;
}
