cc = clang-cl
flags = -nologo -W4 -GS- -Oi -O1 -c -Xclang -std=c11 -Xclang -pedantic
link_flags = -nodefaultlib -subsystem:console -entry:entry main.obj \
			 uuid.lib kernel32.lib user32.lib ole32.lib shell32.lib \
			 -out:prog.exe

all: main.c
	@$(cc) $(flags) main.c && link.exe $(link_flags)
	@mt.exe -manifest main.exe.manifest -outputresource:"prog.exe";1
	@del main.obj
	
clean:
	if exist prog.exe ( 
		del prog.exe 
	)