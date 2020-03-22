WARNING_LEVEL=/nologo /WX /W4 /wd4214 /wd4201
OPTIMIZATION=/Yc /Ox /GFS- /GR- /MD
LINK_SWITCHES=/nologo /release /opt:ref,nowin98,icf=10

strarc.exe: strarc.obj restore.obj backup.obj regsnap.obj linktrack.obj lnk.obj strarc.res ..\lib\minwcrt.lib
	link $(LINK_SWITCHES) strarc.obj restore.obj backup.obj regsnap.obj linktrack.obj lnk.obj strarc.res

strarc.obj: strarc.cpp strarc.hpp ..\include\winstrct.hpp ..\include\winstrct.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) /Fpstrarc strarc.cpp

restore.obj: restore.cpp strarc.hpp lnk.h ..\include\winstrct.hpp ..\include\winstrct.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) /Fprestore restore.cpp

backup.obj: backup.cpp strarc.hpp linktrack.hpp ..\include\winstrct.hpp ..\include\winstrct.h ..\include\wfind.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) /Fpbackup backup.cpp

regsnap.obj: regsnap.cpp strarc.hpp ..\include\winstrct.hpp ..\include\winstrct.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) /Fpregsnap regsnap.cpp

linktrack.obj: linktrack.cpp strarc.hpp linktrack.hpp ..\include\winstrct.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) /Fplinktrack linktrack.cpp

lnk.obj: lnk.c lnk.h ..\include\winstrct.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) /Fplnk lnk.c

strarc.res: strarc.rc
	rc strarc.rc

install: p:\utils\strarc.exe p:\utils\strarc.txt z:\ltr-website\files\strarc.txt.gz

p:\utils\strarc.exe: strarc.exe
	copy /y strarc.exe p:\utils\ 

p:\utils\strarc.txt: strarc.txt
	copy /y strarc.txt p:\utils\ 

z:\ltr-website\files\strarc.txt.gz: strarc.txt
	tr -d \r < strarc.txt | gzip -9 > z:\ltr-website\files\strarc.txt.gz

clean:
	del *.obj *~ *.pch
