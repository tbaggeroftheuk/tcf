# TCF (Tbag Content File)

This neat little file format I made was originally a toy I made in python. 
It was when I was developing a C (now C++) game engine I realised I can use such format for this.
When during development of such game engine I found out python is crap at bit shifting so I made a C packer as well so I now have this.

Hope you enjoy :)

BTW you can use this format in other projects, just grab [TCF.h](./Code/tcf.h) [TCF.c](./Code/tcf.c)
and dump those into your project folder to use them


# Build from source

Whats required:

Clang (or gcc)

```git clone https://github.com/tbaggeroftheuk/tcf```

```cd tcf/Code```

For linux builds: ```clang -Wall -O2 main.c tcf.c -o TCF ```

For Windows builds ```clang --target=x86_64-w64-mingw32 main.c tcf.c -o TCF.exe```

