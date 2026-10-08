#Turns a binary file into a C++ header holding it as a byte array, at configure time.
#Reconfigures when the file changes.
function(embed_file input output name)
    file(READ ${input} hex HEX)
    string(LENGTH "${hex}" hexLength)
    math(EXPR size "${hexLength} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "(0x[0-9a-f][0-9a-f],){24}" "\\0\n    " bytes "${bytes}")
    file(WRITE ${output} "#pragma once\n\n//Generated from ${input} by cmake/EmbedFile.cmake.\nstatic const unsigned char ${name}[${size}] = {\n    ${bytes}\n};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${input})
endfunction()
