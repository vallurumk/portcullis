//  ********************************************************************
//  This file is part of Portcullis.
//
//  Portcullis is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Portcullis is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Portcullis.  If not, see <http://www.gnu.org/licenses/>.
//  *******************************************************************

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <sstream>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
using std::stringstream;
using std::ifstream;
using std::string;
using std::cout;
using std::endl;
using std::vector;

#ifdef HAVE_PYTHON
#include <Python.h>
#endif

#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string/join.hpp>
namespace bfs = boost::filesystem;
using bfs::path;

#include <portcullis/portcullis_fs.hpp>

namespace portcullis {

typedef boost::error_info<struct PortcullisPythonError,string> PortcullisPythonErrorInfo;
struct PortcullisPythonException: virtual boost::exception, virtual std::exception { };

// This class provides an easy way to run embedded python scripts within Portcullis.
// This is a singleton class as the python API can be a bit tetchy, at least the
// way we use it here.  So only one global version of this should be available.
class PyHelper {
private:
    bool verbose;
    wchar_t* py_interp = NULL;

    string full_python_path_str;
    wchar_t* full_python_path_wchar = NULL;

    PyHelper() {
#ifdef PY_DEBUG
        this->verbose = true;
#else
		this->verbose = false;
#endif

#ifdef HAVE_PYTHON

        if (this->verbose) {
            cout << endl << "Initialising python interpreter ..." << endl;
        }

        this->py_interp = Py_DecodeLocale(PYTHON_INT_PATH, NULL);
        Py_SetProgramName(this->py_interp);

        if (this->verbose) {
            std::wstring fullprogrampath = Py_GetProgramFullPath();
            string fpp(fullprogrampath.begin(), fullprogrampath.end());
            cout << " - Interpreter path: " << fpp << endl;
        }

        vector<string> ppaths;

        wchar_t* wtppath2 = Py_GetPath();
        std::wstring wppath2(wtppath2);
        string py_path_interp(wppath2.begin(), wppath2.end());
        ppaths.push_back(py_path_interp);

        string py_path_here(portcullis::pfs.getScriptsDir().string() + ":" + PYTHON_INTERP_SITE_PKGS);
        ppaths.push_back(py_path_here);

        this->full_python_path_str = boost::algorithm::join(ppaths, ":");
        if (this->verbose) {
            cout << " - PYTHONPATH (from env + interpreter) : " << py_path_interp << endl
                 << " - PYTHONPATH (added here)             : " << py_path_here << endl
                 << " - PYTHONPATH (combined)               : " << this->full_python_path_str<< endl;
        }
        this->full_python_path_wchar = Py_DecodeLocale(this->full_python_path_str.c_str(), NULL);
        Py_SetPath(this->full_python_path_wchar);
        if (this->verbose) {
            cout << " - PYTHONPATH set"  << endl;
        }

        Py_Initialize();
        if (this->verbose) {
            cout << "Python interpretter initialised" << endl << endl;
        }
#else
        BOOST_THROW_EXCEPTION(PortcullisPythonException() << PortcullisPythonErrorInfo(string(
                "Portcullis not configured to use Python.  Please check your configuration and rebuild.")));
#endif
    }

public:

    PyHelper(PyHelper const&)        = delete;
    void operator=(PyHelper const&)  = delete;

    static PyHelper& getInstance() {
        static PyHelper instance; // Guaranteed to be destroyed.  Instantiated on first use.
        return instance;
    }

    void execute(const string script_name, int argc, char *argv[]) {
#ifdef HAVE_PYTHON
        if (this->verbose) {
            cout << "Executing python script: " << script_name << " ..." << endl;
        }

        const path scripts_dir = portcullis::pfs.getScriptsDir();
        const path full_script_path = path(scripts_dir.string() + "/" + script_name);

        stringstream ss;

        // Create wide char alternatives
        wchar_t* wsn = Py_DecodeLocale(script_name.c_str(), NULL);
        wchar_t* wsp = Py_DecodeLocale(full_script_path.c_str(), NULL);
        wchar_t* wargv[50]; // Can't use variable length arrays!
        wargv[0] = wsp;
        ss << full_script_path.c_str();
        for(int i = 1; i < argc; i++) {
            wargv[i] = Py_DecodeLocale(argv[i], NULL);
            ss << " " << argv[i];
        }
        for(int i = argc; i < 50; i++) {
            wargv[i] = Py_DecodeLocale("\0", NULL);
        }
        if (this->verbose) {
            cout << " - Setting arguments" << endl;
        }
        PySys_SetArgv(argc, wargv);

        FILE* pf = _Py_fopen(full_script_path.c_str(), "r");
        if (pf == NULL) {
            BOOST_THROW_EXCEPTION(PortcullisPythonException() << PortcullisPythonErrorInfo(string(
                    "Could not open script file as a python file object") + full_script_path.string()));
        }

        if (this->verbose) {
            cout << " - Effective command line: python3 " << ss.str() << endl;
            cout << " - Output from python script follows: " << endl << endl;
        }

        if (PyRun_SimpleFileEx(pf, full_script_path.c_str(), true) != 0) {
            BOOST_THROW_EXCEPTION(PortcullisPythonException() << PortcullisPythonErrorInfo(string(
                    "Unexpected python error")));
        }

        // Cleanup
        PyMem_RawFree(wsn);
        // No need to free up "wsp" as it is element 0 in the array
        for(int i = 0; i < argc; i++) {
            PyMem_RawFree(wargv[i]);
        }

        if (this->verbose) {
            cout << endl << "Python script \"" << script_name << "\" executed successfully" << endl;
        }
#else
        BOOST_THROW_EXCEPTION(PortcullisPythonException() << PortcullisPythonErrorInfo(string(
                "Portcullis not configured to use Python.  Please check your configuration and rebuild.")));
#endif
    }

    ~PyHelper() {
#ifdef HAVE_PYTHON
        Py_Finalize();
        PyMem_RawFree(this->py_interp);
        PyMem_RawFree(this->full_python_path_wchar);
#endif
    }
};

}
