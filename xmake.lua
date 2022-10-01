-- Basic Usage:
--  xmake config -m debug
--  xmake build
--  xmake run unittest
--  xmake install -o C:\opt

set_languages("c")

add_rules("mode.debug", "mode.release")

add_requires("expat","fftw","pthreads4w","openssl")
add_requires("zlib", {system = false})

add_includedirs(".")
add_packages("expat","zlib","fftw","pthreads4w","openssl")
if is_plat("windows") then
    add_cxflags("/TC")
end

-- The Library ---------------------------------------------------------------
target("das2-lib")
    set_kind("static")
    add_files("das2/*.c")
    remove_files("das2/spice.c")
    if is_plat("windows") then
        add_defines("WISDOM_FILE=C:/ProgramData/fftw3/wisdom.dat")
    else
        add_defines("WISDOM_FILE=/etc/fftw3/wisdom.dat")
    end
    set_license("lgpl-2.1")
target_end()

-- Lib Test programs ----------------------------------------------------------

-- Aside: Somehow using the keyword 'local' makes the array aTestProgs seem
--        more global-ish because unittest:on_run() can see it. Without the 
--        "local" keyword aTestProgs is invisible inside unittest:on_run().
--        Lua is... weird.

local aTestProgs = {
    "TestUnits", "TestArray","TestBuilder","TestAuth","TestCatalog",
    "TestTT2000", "LoadStream"
}

-- Target definitions
for i, name in ipairs(aTestProgs) do
    target(name)
       set_kind("binary")
       add_deps("das2-lib")
       add_files("test/" .. name .. ".c")
    target_end()
end 

-- Fake target for running unittests
target("unittest")
    set_kind("phony")
    add_deps(aTestProgs)
    
    on_run(function (target)
        for i, name in ipairs(aTestProgs) do
            sProg = target:targetdir().."/"..name
            print("Running: %s ...", sProg)
            os.exec(sProg)
        end
    end)
target_end()
    

-- Utility Programs -----------------------------------------------------------

local aUtilProgs = { 
    "das1_inctime", "das2_prtime", "das1_fxtime", "das2_ascii", "das2_bin_avg", 
    "das2_bin_avgsec", "das2_bin_peakavgsec", "das2_cache_rdr", "das2_from_das1",
    "das2_from_tagged_das1", "das1_ascii", "das1_bin_avg", "das2_bin_ratesec",
    "das2_psd", "das2_histo"
}

-- Target definitions
for i, name in ipairs(aUtilProgs) do

    target(name)
       set_kind("binary")
       add_deps("das2-lib")
       add_files("utilities/" .. name .. ".c")
        -- Typically these are one lines, but a couple have more than one file
        if name == "das2_bin_ratesec" then
            add_files("utilities/via.c")
        elseif name == "das2_psd" then
            add_files("utilities/send.c")
        end
    target_end()
end 


-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake config -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake config -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro defination
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

