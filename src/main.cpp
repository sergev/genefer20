/*
Copyright 2020, Yves Gallot

genefer20 is free source code, under the MIT license (see LICENSE). You can redistribute, use and/or modify it.
Please give feedback to the authors if improvement is realized. It is distributed in the hope that it will be useful.
*/

#include "pio.h"
#include "ocl.h"
#include "genefer.h"
#include "boinc.h"

#include <cstdlib>
#include <stdexcept>
#include <vector>

#if defined (_WIN32)
#include <Windows.h>
#else
#include <signal.h>
#endif

class application
{
private:
	struct deleter { void operator()(const application * const p) { delete p; } };

private:
	static void quit(int)
	{
		genefer::getInstance().quit();
	}

private:
#if defined (_WIN32)
	static BOOL WINAPI HandlerRoutine(DWORD)
	{
		quit(1);
		return TRUE;
	}
#endif

public:
	application()
	{
#if defined (_WIN32)	
		SetConsoleCtrlHandler(HandlerRoutine, TRUE);
#else
		signal(SIGTERM, quit);
		signal(SIGINT, quit);
#endif
	}

	virtual ~application() {}

	static application & getInstance()
	{
		static std::unique_ptr<application, deleter> pInstance(new application());
		return *pInstance;
	}

private:
	static std::string header(const std::vector<std::string> & args, const bool nl = false)
	{
		const char * const sysver =
#if defined(_WIN64)
			"win64";
#elif defined(_WIN32)
			"win32";
#elif defined(__linux__)
#ifdef __x86_64
			"linux64";
#else
			"linux32";
#endif
#elif defined(__APPLE__)
			"macOS";
#else
			"unknown";
#endif

		std::ostringstream ssc;
#if defined(__GNUC__)
		ssc << " gcc-" << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#elif defined(__clang__)
		ssc << " clang-" << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#endif

		std::ostringstream ss;
		ss << "genefer20 1.13.0 " << sysver << ssc.str() << std::endl;
		ss << "Copyright (c) 2020, Yves Gallot" << std::endl;
		ss << "genefer20 is free source code, under the MIT license." << std::endl;
		if (nl)
		{
			ss << std::endl << "Command line: '";
			bool first = true;
			for (const std::string & arg : args)
			{
				if (first) first = false; else ss << " ";
				ss << arg;
			}
			ss << "'" << std::endl << std::endl;
		}
		return ss.str();
	}

private:
	static std::string usage()
	{
		std::ostringstream ss;
		ss << "Usage: genefer20 [options]  options may be specified in any order" << std::endl;
		ss << "  -n <n>                  GFN exponent (b^{2^n} + 1) " << std::endl;
		ss << "  -f <filename>           input text file (one b per line)" << std::endl;
		ss << "  -d <n> or --device <n>  set device number=<n> (default 0)" << std::endl;
		ss << "  -p                      display results on the screen (default false)" << std::endl;
		ss << "  -v or -V                print the startup banner and immediately exit" << std::endl;
#ifdef BOINC
		ss << "  -boinc                  operate as a BOINC client app" << std::endl;
#endif
		ss << std::endl;
		return ss.str();
	}

public:
	void run(int argc, char * argv[])
	{
		std::vector<std::string> args;
		for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

		bool bBoinc = false;
#ifdef BOINC
		for (const std::string & arg : args) if (arg == "-boinc") bBoinc = true;
#endif
		pio::getInstance().setBoinc(bBoinc);

		cl_platform_id boinc_platform_id = 0;
		cl_device_id boinc_device_id = 0;
		if (bBoinc)
		{
			const int retval = boinc_init();
			if (retval != 0)
			{
				std::ostringstream ss; ss << "boinc_init returned " << retval;
				throw std::runtime_error(ss.str());
			}
#ifdef BOINC
			if (!boinc_is_standalone())
			{
				const int err = boinc_get_opencl_ids(argc, argv, 0, &boinc_device_id, &boinc_platform_id);
				if ((err != 0) || (boinc_device_id == 0) || (boinc_platform_id == 0))
				{
					std::ostringstream ss; ss << std::endl << "error: boinc_get_opencl_ids() failed err = " << err;
					throw std::runtime_error(ss.str());
				}
			}
#endif
		}

		// if -v or -V then print header to stderr and exit
		for (const std::string & arg : args)
		{
			if ((arg[0] == '-') && ((arg[1] == 'v') || (arg[1] == 'V')))
			{
				pio::error(header(args));
				if (bBoinc) boinc_finish(EXIT_SUCCESS);
				return;
			}
		}

		pio::print(header(args, true));

		if (args.empty()) pio::print(usage());	// print usage, display devices and exit

		ocl::platform platform;
		if (platform.displayDevices() == 0) throw std::runtime_error("No OpenCL device");

		if (args.empty()) return;

		size_t d = 0;
		int n = 0;
		std::string filename;	// = "GFN8.txt";	// test
		bool display = false;
		// parse args
		for (size_t i = 0, size = args.size(); i < size; ++i)
		{
			const std::string & arg = args[i];

			if (arg.substr(0, 2) == "-n")
			{
				const std::string nval = ((arg == "-n") && (i + 1 < size)) ? args[++i] : arg.substr(2);
				n = std::atoi(nval.c_str());
				if (n < 8) throw std::runtime_error("n < 8 is not supported");
				if (n > 16) throw std::runtime_error("n > 16 is not supported");
			}
			else if (arg.substr(0, 2) == "-f")
			{
				filename = ((arg == "-f") && (i + 1 < size)) ? args[++i] : arg.substr(2);
			}
			else if (arg.substr(0, 2) == "-d")
			{
				const std::string dev = ((arg == "-d") && (i + 1 < size)) ? args[++i] : arg.substr(2);
				d = std::atoi(dev.c_str());
				if (d >= platform.getDeviceCount()) throw std::runtime_error("invalid device number");
			}
			else if (arg.substr(0, 8) == "--device")
			{
				const std::string dev = ((arg == "--device") && (i + 1 < size)) ? args[++i] : arg.substr(8);
				d = std::atoi(dev.c_str());
				if (d >= platform.getDeviceCount()) throw std::runtime_error("invalid device number");
			}
			if (arg == "-p") display = true;
		}

		if (n == 0) return;

		genefer & gen = genefer::getInstance();
		gen.setBoinc(bBoinc);

		const bool is_boinc_platform = bBoinc && (boinc_device_id != 0) && (boinc_platform_id != 0);
		const ocl::platform eng_platform = is_boinc_platform ? ocl::platform(boinc_platform_id, boinc_device_id) : platform;
		const size_t eng_d = is_boinc_platform ? 0 : d;
		engine eng(eng_platform, eng_d);

		gen.init(n, eng, bBoinc);

		// gen.valid(); return;

		if (!filename.empty())
		{
			gen.checkFile(filename, display);
		}
		else
		{
			gen.bench();
		}

		gen.release();

		if (bBoinc) boinc_finish(EXIT_SUCCESS);
	}
};

int main(int argc, char * argv[])
{
	try
	{
		application & app = application::getInstance();
		app.run(argc, argv);
	}
	catch (const std::runtime_error & e)
	{
		std::ostringstream ss; ss << std::endl << "error: " << e.what() << "." << std::endl;
		pio::error(ss.str(), true);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
