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
(base) $ conda install conda-verify
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

Before building anything for Windows you'll need a compiler.  Microsoft makes this
more difficult to setup then any other operating system I normally work with.  Not
only will you need a copy of visual studio, you need the **particular version** of
visual studio that `conda build` looks for.  The following link has
worked for me for miniconda3.9:

(VS Build Tools 2015)[https://download.visualstudio.microsoft.com/download/pr/3e542575-929e-4297-b6c6-bef34d0ee648/639c868e1219c651793aff537a1d3b77/vs_buildtools.exe]

Install this before proceeding.

Das2C is built to be multi-thread safe.  Thread safety is built around the
POSIX threads library, which is native to Linux and MacOS.  On Windows a
wrapper library must be used instead.  Before building das2C packages you'll
need to build [pthreads4w](https://sourceforge.net/projects/pthreads4w/) 
(POSIX Threads for Windows).  To do so:

```dos
cd das2C\buildfiles
conda build conda_pthreads4w
rem Path below is just an example
anaconda upload -u dasdevelopers \Users\cwp\miniconda39\conda-bld\win-64\pthreads4w-3.0.0-hf4e77e7_0.bz2
conda install -c dasdevelopers pthreads4w
```

## To Build

1. Activate your conda environment

2. Get the sources
   ```bash
   (base) $ git clone https://github.com/das-developers/das2C.git
   (base) $ cd das2C
   ```

3. Run conda build:
   ```bash
   (base) $ conda build buildfiles/conda
   ```

4. Upload to anaconda.org
   ```bash
   (base) $ anaconda upload -u dasdevelopers /Users/cwp/conda-bld/osx-64/das2C-3.0-pre3-py38h1de35cc_0.tar.bz2
   # for example, your exact name will be different
   ```

## Install and test

Have users install via:
```bash
(base) $ conda install -c dasdevelopers das2c
```
And test via:
```bash
(base) $ das2_psd -h
```





