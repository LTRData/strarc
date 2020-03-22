!IF "$(CPU)" == ""
CPU=$(_BUILDARCH)
!ENDIF

!IF "$(CPU)" == ""
CPU=i386
!ENDIF

WARNING_LEVEL=/nologo /WX /W4 /wd4214 /wd4201 /wd4206
!IF "$(CPU)" == "i386"
OPTIMIZATION=/Yc /Ox /GF /GR- /MD
!ELSE
OPTIMIZATION=/Yc /Ox /GFS- /GR- /MD
!ENDIF
LINK_SWITCHES=/nologo /release /opt:nowin98,ref,icf=10 /largeaddressaware

$(CPU)\strarc.exe: $(CPU)\strarc.obj $(CPU)\restore.obj $(CPU)\backup.obj $(CPU)\regsnap.obj $(CPU)\linktrack.obj $(CPU)\lnk.obj strarc.res ..\lib\minwcrt.lib Makefile
	link $(LINK_SWITCHES) /out:$(CPU)\strarc.exe $(CPU)\strarc.obj $(CPU)\restore.obj $(CPU)\backup.obj $(CPU)\regsnap.obj $(CPU)\linktrack.obj $(CPU)\lnk.obj strarc.res

$(CPU)\strarc.obj: strarc.cpp strarc.hpp ..\include\winstrct.hpp ..\include\winstrct.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\strarc /Fo$(CPU)\strarc strarc.cpp

$(CPU)\restore.obj: restore.cpp strarc.hpp lnk.h ..\include\winstrct.hpp ..\include\winstrct.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\restore /Fo$(CPU)\restore restore.cpp

$(CPU)\backup.obj: backup.cpp strarc.hpp linktrack.hpp ..\include\winstrct.hpp ..\include\winstrct.h ..\include\wfind.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\backup /Fo$(CPU)\backup backup.cpp

$(CPU)\regsnap.obj: regsnap.cpp strarc.hpp ..\include\winstrct.hpp ..\include\winstrct.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\regsnap /Fo$(CPU)\regsnap regsnap.cpp

$(CPU)\linktrack.obj: linktrack.cpp strarc.hpp linktrack.hpp ..\include\winstrct.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\linktrack /Fo$(CPU)\linktrack linktrack.cpp

$(CPU)\lnk.obj: lnk.c lnk.h ..\include\winstrct.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(C_DEFINE) /Fp$(CPU)\lnk /Fo$(CPU)\lnk lnk.c

strarc.res: strarc.rc Makefile
	rc strarc.rc

!IF "$(CPU)" == "i386"

install: p:\utils\strarc.exe p:\utils\strarc.txt z:\ltr-website\files\strarc.txt.gz Makefile

p:\utils\strarc.exe: i386\strarc.exe Makefile
	copy /y i386\strarc.exe p:\utils\ 

p:\utils\strarc.txt: strarc.txt Makefile
	copy /y strarc.txt p:\utils\ 

z:\ltr-website\files\strarc.txt.gz: strarc.txt Makefile
	tr -d \r < strarc.txt | gzip -9 > z:\ltr-website\files\strarc.txt.gz

!ENDIF

clean:
	del /s *.obj *~ *.pch
