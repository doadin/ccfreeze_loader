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

#ifndef VERSION
#define VERSION "2.1"
#endif

#ifndef PREFIX
#  ifdef __VMS
#    define PREFIX ""
#  else
#    define PREFIX "/usr/local"
#  endif
#endif

#ifndef VERSION
#define VERSION "2.1"
#endif

#ifndef EXEC_PREFIX
#define EXEC_PREFIX PREFIX
#endif

#ifndef PYTHONPATH
#define PYTHONPATH PREFIX "/lib/python" VERSION ":" \
              EXEC_PREFIX "/lib/python" VERSION "/lib-dynload"
#endif

#ifndef LANDMARK
#define LANDMARK "os.py"
#endif

#ifndef VPATH
#define VPATH "."
#endif

static wchar_t progpath[MAXPATHLEN+1];
static wchar_t lib_python[] = L"lib/python" VERSION;
static wchar_t prefix[MAXPATHLEN+1];
static wchar_t exec_prefix[MAXPATHLEN+1];

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

static void
reduce(wchar_t *dir)
{
    size_t i = wcslen(dir);
    while (i > 0 && dir[i] != SEP)
        --i;
    dir[i] = '\0';
}

static int
isfile(wchar_t *filename)          /* Is file, not directory */
{
    struct stat buf;
    if (_wstat(filename, &buf) != 0)
        return 0;
    if (!S_ISREG(buf.st_mode))
        return 0;
    return 1;
}

static int
ismodule(wchar_t *filename)        /* Is module -- check for .pyc/.pyo too */
{
    if (isfile(filename))
        return 1;

    /* Check for the compiled version of prefix. */
    if (wcslen(filename) < MAXPATHLEN) {
        wcslen(filename, Py_OptimizeFlag ? L"o" : L"c");
        if (isfile(filename))
            return 1;
    }
    return 0;
}

static int
isdir(wchar_t *filename)                   /* Is directory */
{
    struct stat buf;
    if (_wstat(filename, &buf) != 0)
        return 0;
    if (!S_ISDIR(buf.st_mode))
        return 0;
    return 1;
}

static void
joinpath(wchar_t *buffer, wchar_t *stuff)
{
    size_t n, k;
    if (stuff[0] == SEP)
        n = 0;
    else {
        n = wcslen(buffer);
        if (n > 0 && buffer[n-1] != SEP && n < MAXPATHLEN)
            buffer[n++] = SEP;
    }
    if (n > MAXPATHLEN)
    	Py_FatalError("buffer overflow in getpath.c's joinpath()");
    k = wcslen(stuff);
    if (n + k > MAXPATHLEN)
        k = MAXPATHLEN - n;
    wcsncpy(buffer+n, stuff, k);
    buffer[n+k] = '\0';
}

/* copy_absolute requires that path be allocated at least
   MAXPATHLEN + 1 bytes and that p be no more than MAXPATHLEN bytes. */
static void
copy_absolute(wchar_t *path, wchar_t *p)
{
    if (p[0] == SEP)
        wcscpy(path, p);
    else {
        _wgetcwd(path, MAXPATHLEN);
        if (p[0] == '.' && p[1] == SEP)
            p += 2;
        joinpath(path, p);
    }
}

static void
absolutize(wchar_t *path)
{
    wchar_t buffer[MAXPATHLEN + 1];

    if (path[0] == SEP)
        return;
    copy_absolute(buffer, path);
    wcscpy(path, buffer);
}

static int
isxfile(wchar_t *filename)         /* Is executable file */
{
    struct stat buf;
    if (_wstat(filename, &buf) != 0)
        return 0;
    if (!S_ISREG(buf.st_mode))
        return 0;
    if ((buf.st_mode & 0111) == 0)
        return 0;
    return 1;
}

/* search_for_prefix requires that argv0_path be no more than MAXPATHLEN
   bytes long.
*/
static int
search_for_prefix(wchar_t *argv0_path, wchar_t *home)
{
    size_t n;
    wchar_t *vpath;

    /* If PYTHONHOME is set, we believe it unconditionally */
    if (home) {
        wchar_t *delim;
        wcsncpy(prefix, home, MAXPATHLEN);
        delim = wcschr(prefix, DELIM);
        if (delim)
            *delim = L'\0';
        joinpath(prefix, lib_python);
        joinpath(prefix, LANDMARK);
        return 1;
    }

    /* Check to see if argv[0] is in the build directory */
    wcsncpy(prefix, argv0_path);
    joinpath(prefix, L"Modules/Setup");
    if (isfile(prefix)) {
        /* Check VPATH to see if argv0_path is in the build directory. */
        vpath = L"" VPATH;
        wcsncpy(prefix, argv0_path);
        joinpath(prefix, vpath);
        joinpath(prefix, L"Lib");
        joinpath(prefix, LANDMARK);
        if (ismodule(prefix))
            return -1;
    }

    /* Search from argv0_path, until root is found */
    copy_absolute(prefix, argv0_path);
    do {
        n = wcslen(prefix);
        joinpath(prefix, lib_python);
        joinpath(prefix, LANDMARK);
        if (ismodule(prefix))
            return 1;
        prefix[n] = L'\0';
        reduce(prefix);
    } while (prefix[0]);

    /* Look at configure's PREFIX */
    wcsncpy(prefix, L"" PREFIX, MAXPATHLEN);
    joinpath(prefix, lib_python);
    joinpath(prefix, LANDMARK);
    if (ismodule(prefix))
        return 1;

    /* Fail */
    return 0;
}


/* search_for_exec_prefix requires that argv0_path be no more than
   MAXPATHLEN bytes long.
*/
static int
search_for_exec_prefix(wchar_t *argv0_path, wchar_t *home)
{
    size_t n;

    /* If PYTHONHOME is set, we believe it unconditionally */
    if (home) {
        wchar_t *delim;
        delim = wcschr(home, DELIM);
        if (delim)
            wcsncpy(exec_prefix, delim+1, MAXPATHLEN);
        else
            wcsncpy(exec_prefix, home, MAXPATHLEN);
        joinpath(exec_prefix, lib_python);
        joinpath(exec_prefix, L"lib-dynload");
        return 1;
    }

    /* Check to see if argv[0] is in the build directory */
    wcscpy(exec_prefix, argv0_path);
    joinpath(exec_prefix, L"Modules/Setup");
    if (isfile(exec_prefix)) {
        reduce(exec_prefix);
        return -1;
    }

    /* Search from argv0_path, until root is found */
    copy_absolute(exec_prefix, argv0_path);
    do {
        n = wcslen(exec_prefix);
        joinpath(exec_prefix, lib_python);
        joinpath(exec_prefix, L"lib-dynload");
        if (isdir(exec_prefix))
            return 1;
        exec_prefix[n] = L'\0';
        reduce(exec_prefix);
    } while (exec_prefix[0]);

    /* Look at configure's EXEC_PREFIX */
    wcsncpy(exec_prefix, L"" EXEC_PREFIX, MAXPATHLEN);
    joinpath(exec_prefix, lib_python);
    joinpath(exec_prefix, L"lib-dynload");
    if (isdir(exec_prefix))
        return 1;

    /* Fail */
    return 0;
}

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
    extern wchar_t *My_Py_GetProgramName(void);

    static wchar_t delimiter[2] = {DELIM, '\0'};
    static wchar_t separator[2] = {SEP, '\0'};
    wchar_t *pythonpath = PYTHONPATH;
    wchar_t *rtpypath = Py_GETENV("PYTHONPATH");
    wchar_t *home = Py_GetPythonHome();
    wchar_t *path = getenv("PATH");
    wchar_t *prog = Py_GetProgramName();
    wchar_t argv0_path[MAXPATHLEN+1];
    wchar_t zip_path[MAXPATHLEN+1];
    int pfound, efound; /* 1 if found; -1 if found build directory */
    wchar_t *buf;
    size_t bufsz;
    size_t prefixsz;
    wchar_t *defpath = pythonpath;
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
	if (wcschr(prog, SEP))
		wcsncpy(progpath, prog, MAXPATHLEN);
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
			wchar_t *delim = wcschr(path, DELIM);

			if (delim) {
				size_t len = delim - path;
				if (len > MAXPATHLEN)
					len = MAXPATHLEN;
				wcsncpy(progpath, path, len);
				*(progpath + len) = '\0';
			}
			else
				wcsncpy(progpath, path, MAXPATHLEN);

			joinpath(progpath, prog);
			if (isxfile(progpath))
				break;

			if (!delim) {
				progpath[0] = L'\0';
				break;
			}
			path = delim + 1;
		}
	}
	else
		progpath[0] = '\0';
	if (progpath[0] != SEP)
		absolutize(progpath);
	wcsncpy(argv0_path, progpath, MAXPATHLEN);
	argv0_path[MAXPATHLEN] = '\0';

#ifdef WITH_NEXT_FRAMEWORK
	/* On Mac OS X we have a special case if we're running from a framework.
	** This is because the python home should be set relative to the library,
	** which is in the framework, not relative to the executable, which may
	** be outside of the framework. Except when we're in the build directory...
	*/
    pythonModule = NSModuleForSymbol(NSLookupAndBindSymbol("_Py_Initialize"));
    /* Use dylib functions to find out where the framework was loaded from */
    buf = (wchar_t *)NSLibraryNameForModule(pythonModule);
    if (buf != NULL) {
        /* We're in a framework. */
        /* See if we might be in the build directory. The framework in the
        ** build directory is incomplete, it only has the .dylib and a few
        ** needed symlinks, it doesn't have the Lib directories and such.
        ** If we're running with the framework from the build directory we must
        ** be running the interpreter in the build directory, so we use the
        ** build-directory-specific logic to find Lib and such.
        */
        wcsncpy(argv0_path, buf, MAXPATHLEN);
        reduce(argv0_path);
        joinpath(argv0_path, lib_python);
        joinpath(argv0_path, LANDMARK);
        if (!ismodule(argv0_path)) {
                /* We are in the build directory so use the name of the
                   executable - we know that the absolute path is passed */
                wcsncpy(argv0_path, prog, MAXPATHLEN);
        }
        else {
                /* Use the location of the library as the progpath */
                wcsncpy(argv0_path, buf, MAXPATHLEN);
        }
    }
#endif

#if HAVE_READLINK
    {
        wchar_t tmpbuffer[MAXPATHLEN+1];
        int linklen = readlink(progpath, tmpbuffer, MAXPATHLEN);
        while (linklen != -1) {
            /* It's not null terminated! */
            tmpbuffer[linklen] = '\0';
            if (tmpbuffer[0] == SEP)
                /* tmpbuffer should never be longer than MAXPATHLEN,
                   but extra check does not hurt */
                wcsncpy(argv0_path, tmpbuffer, MAXPATHLEN);
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
        wcsncpy(prefix, L"" PREFIX, MAXPATHLEN);
        joinpath(prefix, lib_python);
    }
    else
        reduce(prefix);

    wcsncpy(zip_path, prefix, MAXPATHLEN);
    zip_path[MAXPATHLEN] = L'\0';
    if (pfound > 0) { /* Use the reduced prefix returned by Py_GetPrefix() */
        reduce(zip_path);
        reduce(zip_path);
    }
    else
        wcsncpy(zip_path, L"" PREFIX, MAXPATHLEN);
    joinpath(zip_path, L"lib/python00.zip");
    bufsz = wcslen(zip_path);	/* Replace "00" with version */
    zip_path[bufsz - 6] = VERSION[0];
    zip_path[bufsz - 5] = VERSION[2];

    if (!(efound = search_for_exec_prefix(argv0_path, home))) {
        if (!Py_FrozenFlag)
            fprintf(stderr,
                "Could not find platform dependent libraries <exec_prefix>\n");
        wcsncpy(exec_prefix, L"" EXEC_PREFIX, MAXPATHLEN);
        joinpath(exec_prefix, L"lib/lib-dynload");
    }
    /* If we found EXEC_PREFIX do *not* reduce it!  (Yet.) */

    if ((!pfound || !efound) && !Py_FrozenFlag)
        fprintf(stderr,
                "Consider setting $PYTHONHOME to <prefix>[:<exec_prefix>]\n");

    /* Calculate size of return buffer.
     */
    bufsz = 0;

    if (rtpypath)
        bufsz += wcslen(rtpypath) + 1;

    prefixsz = wcslen(prefix) + 1;

    while (1) {
        wchar_t *delim = wcschr(defpath, DELIM);

        if (defpath[0] != SEP)
            /* Paths are relative to prefix */
            bufsz += prefixsz;

        if (delim)
            bufsz += delim - defpath + 1;
        else {
            bufsz += wcslen(defpath) + 1;
            break;
        }
        defpath = delim + 1;
    }

    bufsz += wcslen(zip_path) + 1;
    bufsz += wcslen(exec_prefix) + 1;

    /* This is the only malloc call in this file */
    buf = (wchar_t *)PyMem_Malloc(bufsz*sizeof(wchar_t));

    if (buf == NULL) {
        /* We can't exit, so print a warning and limp along */
        fprintf(stderr, "Not enough memory for dynamic PYTHONPATH.\n");
        fprintf(stderr, "Using default static PYTHONPATH.\n");
        module_search_path = L"" PYTHONPATH;
    }
    else {
        /* Run-time value of $PYTHONPATH goes first */
        if (rtpypath) {
            wcscpy(buf, rtpypath);
            wcscat(buf, delimiter);
        }
        else
            buf[0] = '\0';

        /* Next is the default zip path */
        wcscat(buf, zip_path);
        wcscat(buf, delimiter);

        /* Next goes merge of compile-time $PYTHONPATH with
         * dynamically located prefix.
         */
        defpath = pythonpath;
        while (1) {
            wchar_t *delim = wcschr(defpath, DELIM);

            if (defpath[0] != SEP) {
                wcscat(buf, prefix);
                wcscat(buf, separator);
            }

            if (delim) {
                size_t len = delim - defpath + 1;
                size_t end = wcslen(buf) + len;
                strncat(buf, defpath, len);
                *(buf + end) = '\0';
            }
            else {
                wcscat(buf, defpath);
                break;
            }
            defpath = delim + 1;
        }
        wcscat(buf, delimiter);

        /* Finally, on goes the directory for dynamic-load modules */
        wcscat(buf, exec_prefix);

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
		wcscpy(prefix, separator);
    }
    else
        wcscpy(prefix, L"" PREFIX, MAXPATHLEN);

    if (efound > 0) {
        reduce(exec_prefix);
        reduce(exec_prefix);
        reduce(exec_prefix);
	if (!exec_prefix[0])
		strcpy(exec_prefix, separator);
    }
    else
        wcscpy(exec_prefix, L"" EXEC_PREFIX, MAXPATHLEN);
}

wchar_t *
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
