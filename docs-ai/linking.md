# linking

The compiler links only its own output and uses no libc and no C runtime.
The emit option selects exe, obj, ir, bc, header, static or shared output.
Linux uses ld.lld or ld, macos uses ld64.lld, and windows uses lld-link, all found on the path.
A missing linker is an error, and emitting obj needs no linker.
Static archives are written in-process without ar.
The linker driver passes no libraries on linux and macos.
On windows the at library attribute generates import libraries from the declared symbols with llvm-dlltool.
A declared function that no module defines warns with W0301.
There is no command-line option for extra objects, and the package manager and project file docs describe unimplemented design.
