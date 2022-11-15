/* Replacements for setenv, unsetenv on windows */

/* Add #include <stdlib.h> to your function to then include this in your 
   module implementation file (*.c) not in a header */

int setenv(const char *name, const char *value, int overwrite)
{
	int errcode = 0;
	if(!overwrite) {
		size_t envsize = 0;
		errcode = getenv_s(&envsize, NULL, 0, name);
		if(errcode || envsize) return errcode;
	}
	return _putenv_s(name, value);
}

int unsetenv(const char *name)
{
	const char* value = "";
	return _putenv_s(name, value);  // empty string removes the value
}