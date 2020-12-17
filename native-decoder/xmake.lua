add_rules("mode.debug", "mode.release")

set_languages("cxx17")
set_warnings("all", "warn")
add_includedirs("$(projectdir)", "$(projectdir)/thirdparty")

add_defines("USE_STANDALONE_ASIO", "ASIO_STANDALONE")

if is_plat("mingw32") then
    add_syslinks("wsock32", "ws2_32", "bcrypt", "pthread")
    --add_defines("_GLIBCXX_HAS_GTHREADS=1")
    add_defines("_WIN32_WINNT=0x0600", "_WEBSOCKETPP_CPP11_STL_", "_WEBSOCKETPP_CPP11_THREAD_")
    add_cxxflags("-Wa,-mbig-obj")
else
    add_includedirs("/usr/local/include")
    add_linkdirs("/usr/local/lib")
    add_syslinks("ssl", "crypto", "pthread", "dl")
end

add_ldflags("-static")

add_links("avformat", "avcodec", "avutil", "swscale", "swresample", "x264", "x265", "z")

target("native-decoder")
	set_kind("binary")
    add_files("cmd/*.cc")
    