# Conda build instructions

Install miniconda (no need to deal with the huge full version).  When asked
for shell integration say "no".  (It's actually "Hell No" but that's not a
valid answer.)

Enter the conda environment:
```bash
source ~/miniconda3/bin/activate
```

Get the build tools:

```bash
(base) $ conda install conda-build
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

Have users update:
```bash
(base) $ conda update -c dasdevelopers das2C
```