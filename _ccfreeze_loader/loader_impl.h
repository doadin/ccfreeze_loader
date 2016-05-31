// run frozen programs

#include <Python.h>
#include <osdefs.h>

#ifdef WIN32
#include <windows.h>
#endif

#include <compile.h>
#include <eval.h>

#include <limits.h>
#include <stdio.h>

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

static char *module_search_path = NULL;

static void fatal(const char *message)
{
#ifdef GUI
	MessageBox(NULL, message, "ccfreeze Fatal Error", MB_ICONERROR);
#else
	fprintf(stderr, "Fatal error: %s", message);	
#endif
	exit(1);
}


static char *syspath = 0;

static void dirname(const char *path)
{
	char *lastsep = strrchr(path, SEP);
	if (lastsep==0) {
		fatal("dirname failed.");
	}
	*lastsep = 0;
}

static void compute_syspath(void)
{
	const char *resolved_path = strdup(Py_GetProgramFullPath());

	dirname(resolved_path);
	syspath = malloc(2*strlen(resolved_path)+64);

	sprintf(syspath, "%s%clibrary.zip%c%s", resolved_path, SEP, DELIM, resolved_path);
	//fprintf(stderr, "syspath: %s\n", syspath);
}

static void
calculate_path(void)
{
    extern char *Py_GetProgramName(void);

    static char delimiter[2] = {DELIM, '\0'};
    static char separator[2] = {SEP, '\0'};
    char *pythonpath = PYTHONPATH;
    char *rtpypath = Py_GETENV("PYTHONPATH");
    char *home = Py_GetPythonHome();
    char *path = getenv("PATH");
    char *prog = Py_GetProgramName();
    char argv0_path[MAXPATHLEN+1];
    char zip_path[MAXPATHLEN+1];
    int pfound, efound; /* 1 if found; -1 if found build directory */
    char *buf;
    size_t bufsz;
    size_t prefixsz;
    char *defpath = pythonpath;
#ifdef WITH_NEXT_FRAMEWORK
    NSModule pythonModule;
#endif
#ifdef __APPLE__
#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
    uint32_t nsexeclength = MAXPATHLEN;
#else
    unsigned long nsexeclength = MAXPATHLEN;
#endif
#endif

	/* If there is no slash in the argv0 path, then we have to
	 * assume python is on the user's $PATH, since there's no
	 * other way to find a directory to start the search from.  If
	 * $PATH isn't exported, you lose.
	 */
	if (strchr(prog, SEP))
		strncpy(progpath, prog, MAXPATHLEN);
#ifdef __APPLE__
     /* On Mac OS X, if a script uses an interpreter of the form
      * "#!/opt/python2.3/bin/python", the kernel only passes "python"
      * as argv[0], which falls through to the $PATH search below.
      * If /opt/python2.3/bin isn't in your path, or is near the end,
      * this algorithm may incorrectly find /usr/bin/python. To work
      * around this, we can use _NSGetExecutablePath to get a better
      * hint of what the intended interpreter was, although this
      * will fail if a relative path was used. but in that case,
      * absolutize() should help us out below
      */
     else if(0 == _NSGetExecutablePath(progpath, &nsexeclength) && progpath[0] == SEP)
       ;
#endif /* __APPLE__ */
	else if (path) {
		while (1) {
			char *delim = strchr(path, DELIM);

			if (delim) {
				size_t len = delim - path;
				if (len > MAXPATHLEN)
					len = MAXPATHLEN;
				strncpy(progpath, path, len);
				*(progpath + len) = '\0';
			}
			else
				strncpy(progpath, path, MAXPATHLEN);

			joinpath(progpath, prog);
			if (isxfile(progpath))
				break;

			if (!delim) {
				progpath[0] = '\0';
				break;
			}
			path = delim + 1;
		}
	}
	else
		progpath[0] = '\0';
	if (progpath[0] != SEP)
		absolutize(progpath);
	strncpy(argv0_path, progpath, MAXPATHLEN);
	argv0_path[MAXPATHLEN] = '\0';

#ifdef WITH_NEXT_FRAMEWORK
	/* On Mac OS X we have a special case if we're running from a framework.
	** This is because the python home should be set relative to the library,
	** which is in the framework, not relative to the executable, which may
	** be outside of the framework. Except when we're in the build directory...
	*/
    pythonModule = NSModuleForSymbol(NSLookupAndBindSymbol("_Py_Initialize"));
    /* Use dylib functions to find out where the framework was loaded from */
    buf = (char *)NSLibraryNameForModule(pythonModule);
    if (buf != NULL) {
        /* We're in a framework. */
        /* See if we might be in the build directory. The framework in the
        ** build directory is incomplete, it only has the .dylib and a few
        ** needed symlinks, it doesn't have the Lib directories and such.
        ** If we're running with the framework from the build directory we must
        ** be running the interpreter in the build directory, so we use the
        ** build-directory-specific logic to find Lib and such.
        */
        strncpy(argv0_path, buf, MAXPATHLEN);
        reduce(argv0_path);
        joinpath(argv0_path, lib_python);
        joinpath(argv0_path, LANDMARK);
        if (!ismodule(argv0_path)) {
                /* We are in the build directory so use the name of the
                   executable - we know that the absolute path is passed */
                strncpy(argv0_path, prog, MAXPATHLEN);
        }
        else {
                /* Use the location of the library as the progpath */
                strncpy(argv0_path, buf, MAXPATHLEN);
        }
    }
#endif

#if HAVE_READLINK
    {
        char tmpbuffer[MAXPATHLEN+1];
        int linklen = readlink(progpath, tmpbuffer, MAXPATHLEN);
        while (linklen != -1) {
            /* It's not null terminated! */
            tmpbuffer[linklen] = '\0';
            if (tmpbuffer[0] == SEP)
                /* tmpbuffer should never be longer than MAXPATHLEN,
                   but extra check does not hurt */
                strncpy(argv0_path, tmpbuffer, MAXPATHLEN);
            else {
                /* Interpret relative to progpath */
                reduce(argv0_path);
                joinpath(argv0_path, tmpbuffer);
            }
            linklen = readlink(argv0_path, tmpbuffer, MAXPATHLEN);
        }
    }
#endif /* HAVE_READLINK */

    reduce(argv0_path);
    /* At this point, argv0_path is guaranteed to be less than
       MAXPATHLEN bytes long.
    */

    if (!(pfound = search_for_prefix(argv0_path, home))) {
        if (!Py_FrozenFlag)
            fprintf(stderr,
                "Could not find platform independent libraries <prefix>\n");
        strncpy(prefix, PREFIX, MAXPATHLEN);
        joinpath(prefix, lib_python);
    }
    else
        reduce(prefix);

    strncpy(zip_path, prefix, MAXPATHLEN);
    zip_path[MAXPATHLEN] = '\0';
    if (pfound > 0) { /* Use the reduced prefix returned by Py_GetPrefix() */
        reduce(zip_path);
        reduce(zip_path);
    }
    else
        strncpy(zip_path, PREFIX, MAXPATHLEN);
    joinpath(zip_path, "lib/python00.zip");
    bufsz = strlen(zip_path);	/* Replace "00" with version */
    zip_path[bufsz - 6] = VERSION[0];
    zip_path[bufsz - 5] = VERSION[2];

    if (!(efound = search_for_exec_prefix(argv0_path, home))) {
        if (!Py_FrozenFlag)
            fprintf(stderr,
                "Could not find platform dependent libraries <exec_prefix>\n");
        strncpy(exec_prefix, EXEC_PREFIX, MAXPATHLEN);
        joinpath(exec_prefix, "lib/lib-dynload");
    }
    /* If we found EXEC_PREFIX do *not* reduce it!  (Yet.) */

    if ((!pfound || !efound) && !Py_FrozenFlag)
        fprintf(stderr,
                "Consider setting $PYTHONHOME to <prefix>[:<exec_prefix>]\n");

    /* Calculate size of return buffer.
     */
    bufsz = 0;

    if (rtpypath)
        bufsz += strlen(rtpypath) + 1;

    prefixsz = strlen(prefix) + 1;

    while (1) {
        char *delim = strchr(defpath, DELIM);

        if (defpath[0] != SEP)
            /* Paths are relative to prefix */
            bufsz += prefixsz;

        if (delim)
            bufsz += delim - defpath + 1;
        else {
            bufsz += strlen(defpath) + 1;
            break;
        }
        defpath = delim + 1;
    }

    bufsz += strlen(zip_path) + 1;
    bufsz += strlen(exec_prefix) + 1;

    /* This is the only malloc call in this file */
    buf = (char *)PyMem_Malloc(bufsz);

    if (buf == NULL) {
        /* We can't exit, so print a warning and limp along */
        fprintf(stderr, "Not enough memory for dynamic PYTHONPATH.\n");
        fprintf(stderr, "Using default static PYTHONPATH.\n");
        module_search_path = PYTHONPATH;
    }
    else {
        /* Run-time value of $PYTHONPATH goes first */
        if (rtpypath) {
            strcpy(buf, rtpypath);
            strcat(buf, delimiter);
        }
        else
            buf[0] = '\0';

        /* Next is the default zip path */
        strcat(buf, zip_path);
        strcat(buf, delimiter);

        /* Next goes merge of compile-time $PYTHONPATH with
         * dynamically located prefix.
         */
        defpath = pythonpath;
        while (1) {
            char *delim = strchr(defpath, DELIM);

            if (defpath[0] != SEP) {
                strcat(buf, prefix);
                strcat(buf, separator);
            }

            if (delim) {
                size_t len = delim - defpath + 1;
                size_t end = strlen(buf) + len;
                strncat(buf, defpath, len);
                *(buf + end) = '\0';
            }
            else {
                strcat(buf, defpath);
                break;
            }
            defpath = delim + 1;
        }
        strcat(buf, delimiter);

        /* Finally, on goes the directory for dynamic-load modules */
        strcat(buf, exec_prefix);

        /* And publish the results */
        module_search_path = buf;
    }

    /* Reduce prefix and exec_prefix to their essence,
     * e.g. /usr/local/lib/python1.5 is reduced to /usr/local.
     * If we're loading relative to the build directory,
     * return the compiled-in defaults instead.
     */
    if (pfound > 0) {
        reduce(prefix);
        reduce(prefix);
	/* The prefix is the root directory, but reduce() chopped
	 * off the "/". */
	if (!prefix[0])
		strcpy(prefix, separator);
    }
    else
        strncpy(prefix, PREFIX, MAXPATHLEN);

    if (efound > 0) {
        reduce(exec_prefix);
        reduce(exec_prefix);
        reduce(exec_prefix);
	if (!exec_prefix[0])
		strcpy(exec_prefix, separator);
    }
    else
        strncpy(exec_prefix, EXEC_PREFIX, MAXPATHLEN);
}

char *
My_Py_GetPath(void)
{
    if (!module_search_path)
        calculate_path();
    /* return module_search_path; */

    Py_GetPath();
    compute_syspath();
    return syspath;
}


static int run_script(void)
{
	PyObject *locals;
	PyObject *tmp;

	// sys.path isn't as empty as one might hope. when using zipfile compression
	// the zlib module must be loaded. without cleaning sys.path here, it did happen 
	// that trying to run a frozen executable sometimes did, and sometimes did not find 
	// zlib depending on the way you called the frozen executable (i.e. 'dist/foobar'
	// vs 'foobar'). When cleaning sys.path with the following code, it will always 
	// fail :)
	
	
	locals = PyDict_New();
	if (!locals) {
		fatal("PyDict_New failed.");
	}

	PyDict_SetItemString(locals, "__builtins__", PyEval_GetBuiltins());

	tmp = PyRun_String(
		"import sys\n"
		"del sys.path[2:]\n"
		"sys.frozen=1\n"
		"import zipimport\n"
		"importer = zipimport.zipimporter(sys.path[0])\n"
		"exec importer.get_code('__main__')\n",
		Py_file_input, locals, 0
		);

	Py_DECREF(locals);

	if (!tmp) {
		PyErr_Print();
	}

	Py_Finalize();
	return tmp ? 0 : 1;
}

static void set_program_path(char *argv0)
{
#ifndef WIN32
	static char progpath[PATH_MAX+1];
	int count;

	count=readlink("/proc/self/exe", progpath, PATH_MAX);
	if (count < 0) {
		count = readlink("/proc/curproc/file", progpath, PATH_MAX);
	}
	if (count < 0) {
		Py_SetProgramName(argv0);
	} else {
		progpath[count] = 0;
		Py_SetProgramName(progpath);
	}
#else
	Py_SetProgramName(argv0);
#endif
}

static int loader_main(int argc, char **argv)
{
	// make stdin, stdout and stderr unbuffered
	setbuf(stdin, (char *)NULL);
	setbuf(stdout, (char *)NULL);
	setbuf(stderr, (char *)NULL);

	// initialize Python
	Py_NoSiteFlag = 1;
	Py_FrozenFlag = 1;
	Py_IgnoreEnvironmentFlag = 1;
#if PY_VERSION_HEX >= 0x02060000
	Py_DontWriteBytecodeFlag = 1;
	Py_NoUserSiteDirectory = 1;
#endif
	Py_SetPythonHome("");

	set_program_path(argv[0]);
	Py_Initialize();
	PySys_SetArgv(argc, argv);
#ifdef WIN32
	compute_syspath();
	PySys_SetPath(syspath);
#else
	PySys_SetPath(My_Py_GetPath());
#endif
	return run_script();
}
