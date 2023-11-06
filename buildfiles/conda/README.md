# Conda Package Build Instructions

Install miniconda (no need to deal with the huge full version).  When asked
for shell integration say "no".  (It's actually "Hell No" but that's not a
valid answer.)

Enter the conda environment:
```bash
source ~/miniconda3/bin/activate
```

Get the build and upload tools:

```bash
(base) $ conda install conda-build
(base) $ conda install anaconda-client
```

## MacOS Notes
Special extra hassles for **Intel** MacOS. Note do **not** do this for M1/M2 etc.!

Download the SDK matching your version of MacOS and unzip it to /opt 
(just like the Anaconda default):
```bash
# General page at: https://github.com/phracker/MacOSX-SDKs/releases
cd ~/
wget https://github.com/phracker/MacOSX-SDKs/releases/download/11.3/MacOSX10.13.sdk.tar.xz
xz -d MacOSX10.10.sdk.tar.xz
cd /opt
sudo tar -xvf $HOME/MacOSX10.10.sdk.tar.xz
```
Next for MacOS, setup a `$HOME/conda_build_config.yaml` file with the following contents.
Again, your MacOS version may vary:
```yaml
CONDA_BUILD_SYSROOT:
  - /opt/MacOSX10.10.sdk        # [osx]
```

## Windows Notes

Before building anything for Windows you'll need a compiler.  Microsoft makes
this more difficult to setup then any other operating system I normally work with.

In the \notes directory of this repository you'll find a file named: 
  [install_visual_studio.txt](https://github.com/das-developers/das2C/blob/master/notes/install_visual_studio.txt)
Follow along there until you can run:
```
  \opt\vs\2022\buildtools\Common7\Tools\VsDevCmd.bat
```
You'll need to invoke this command **after** entering the conda environment.  Once
this is issued, move on to creating the pthreads4u and then the das2C packages.

Das2C is built to be multi-thread safe.  Thread safety is built around the
POSIX threads library, which is native to Linux and MacOS.  On Windows a
wrapper library must be used instead.  Before building das2C packages you'll
need to build pthreads4w (POSIX Threads for Windows).  To do so:

```dos
cd das2C\buildfiles
conda build conda_pthreads4w
rem Path below is just an example
anaconda upload -u dasdevelopers \Users\cwp\miniconda39\conda-bld\win-64\pthreads4w-3.0.0-hf4e77e7_0.bz2
conda install -c dasdevelopers pthreads4w
```

## To Build

Change to this directory:
```bash
(base) $ git clone https://github.com/das-developers/das2C.git
(base) $ cd das2C/buildfiles
```

Run conda build:

*Note: On windows remember to load vcvars.bat or equivalent before starting.
(TODO: Find out how to link conda-build to a particular version of visual
studio tools)*

```bash
(base) $ conda build conda  # Kinda weird, 2nd conda is a directory name
```

Upload to anaconda.org
```bash
(base) $ anaconda upload -u dasdevelopers /Users/cwp/conda-bld/osx-64/das2C-2.3-pre3-py38h1de35cc_0.tar.bz2
         (or similar)
```

Have users install via:
```bash
(base) $ conda install -c dasdevelopers das2c
```
Or update via:
```bash
(base) $ conda update -c dasdevelopers das2c
```
And test via:
```bash
(base) $ das2_psd -h
```





