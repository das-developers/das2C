# Conda build instructions

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

Special extra hassles for MacOS, download the SDK matching your version of MacOS
and unzip it to /opt (just like the Anaconda default):
```bash
# General page at: https://github.com/phracker/MacOSX-SDKs/releases
cd ~/
wget https://github.com/phracker/MacOSX-SDKs/releases/download/11.3/MacOSX10.13.sdk.tar.xz
xz -d MacOSX10.13.sdk.tar.xz
cd /opt
sudo tar -xvf $HOME/MacOSX10.13.sdk.tar.xz
```
Next for MacOS, setup a `$HOME/conda_build_config.yaml` file with the following contents.
Again, your MacOS version may vary:
```yaml
CONDA_BUILD_SYSROOT:
  - /opt/MacOSX10.13.sdk        # [osx]
```


Get this directory:
```bash
(base) $ git clone https://github.com/das-developers/das2C.git
(base) $ cd das2C/makefiles
```

Run conda build:
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





