!IF "$(_BUILDARCH)" != ""
CPU=$(_BUILDARCH)
!ENDIF

!IF "$(CPU)" == ""
CPU=i386
!ENDIF

WARNING_LEVEL=/nologo /WX /W4 /wd4214 /wd4201 /wd4206 /wd4996

!IF "$(DEBUG)" == ""

!IF "$(CPU)" == "i386"

OPTIMIZATION=/Yc /Ox /GF /GR- /EHs-c- /Zi /Zc:wchar_t /MD
LINK_SWITCHES=/nologo /release /opt:nowin98,ref,icf=10 /largeaddressaware /debug /defaultlib:bufferoverflowU.lib

!ELSEIF "$(CPU)" == "ARM"

OPTIMIZATION=/Yc /Ox /GFS- /GR- /EHs-c- /Zi /Zc:wchar_t /wd4005 /D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE /MD
LINK_SWITCHES=/nologo /release /opt:ref,icf=10 /largeaddressaware /debug /defaultlib:bufferoverflowU.lib

!ELSEIF "$(CPU)" == "ARM64"

OPTIMIZATION=/Yc /Ox /GFS- /GR- /EHs-c- /Zi /Zc:wchar_t /wd4005 /D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE /MD
LINK_SWITCHES=/nologo /release /opt:ref,icf=10 /largeaddressaware /debug

!ELSE

OPTIMIZATION=/Yc /Ox /GFS- /GR- /EHs-c- /Zi /Zc:wchar_t /MD
LINK_SWITCHES=/nologo /release /opt:nowin98,ref,icf=10 /largeaddressaware /debug /defaultlib:bufferoverflowU.lib

!ENDIF

!ELSE

!IF "$(CPU)" == "i386"

OPTIMIZATION=/Zi /GF /GR- /EHs-c- /Zc:wchar_t /MD
LINK_SWITCHES=/nologo /release /largeaddressaware /debug /defaultlib:bufferoverflowU.lib

!ELSEIF "$(CPU)" == "ARM"

OPTIMIZATION=/Zi /GF /GR- /EHs-c- /Zc:wchar_t /wd4005 /D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE /MD
LINK_SWITCHES=/nologo /release /largeaddressaware /debug

!ELSEIF "$(CPU)" == "ARM64"

OPTIMIZATION=/Zi /GF /GR- /EHs-c- /Zc:wchar_t /wd4005 /D_ARM_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE /MD
LINK_SWITCHES=/nologo /release /largeaddressaware /debug

!ELSE

OPTIMIZATION=/Zi /GF /GR- /EHs-c- /Zc:wchar_t /MD
LINK_SWITCHES=/nologo /release /largeaddressaware /debug /defaultlib:bufferoverflowU.lib

!ENDIF

!ENDIF

all: $(CPU)\strarc.lib $(CPU)\strarc.exe

$(CPU)\strarc.exe: ..\lib\minwcrt.lib Makefile                              $(CPU)\exemain.obj $(CPU)\strarc.obj $(CPU)\parsecmd.obj $(CPU)\constnam.obj $(CPU)\restore.obj $(CPU)\backup.obj $(CPU)\regsnap.obj $(CPU)\bfcopy.obj $(CPU)\lnk.obj strarc.res
	link $(LINK_SWITCHES) /out:$(CPU)\strarc.exe /pdb:$(CPU)\strarc.pdb $(CPU)\exemain.obj $(CPU)\strarc.obj $(CPU)\parsecmd.obj $(CPU)\constnam.obj $(CPU)\restore.obj $(CPU)\backup.obj $(CPU)\regsnap.obj $(CPU)\bfcopy.obj $(CPU)\lnk.obj strarc.res

$(CPU)\strarc.lib: ..\lib\minwcrt.lib Makefile                                                 $(CPU)\strarc.obj $(CPU)\parsecmd.obj $(CPU)\constnam.obj $(CPU)\restore.obj $(CPU)\backup.obj $(CPU)\regsnap.obj $(CPU)\bfcopy.obj $(CPU)\lnk.obj
	lib /out:$(CPU)\strarc.lib                                                             $(CPU)\strarc.obj $(CPU)\parsecmd.obj $(CPU)\constnam.obj $(CPU)\restore.obj $(CPU)\backup.obj $(CPU)\regsnap.obj $(CPU)\bfcopy.obj $(CPU)\lnk.obj

$(CPU)\strarc.obj: strarc.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\strarc /Fo$(CPU)\strarc strarc.cpp

$(CPU)\exemain.obj: exemain.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\exemain /Fo$(CPU)\exemain exemain.cpp

$(CPU)\apifunc.obj: apifunc.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\apifunc /Fo$(CPU)\apifunc apifunc.cpp

$(CPU)\parsecmd.obj: parsecmd.cpp strarc.hpp version.h
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\parsecmd /Fo$(CPU)\parsecmd parsecmd.cpp

$(CPU)\constnam.obj: constnam.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\constnam /Fo$(CPU)\constnam constnam.cpp

$(CPU)\restore.obj: restore.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\restore /Fo$(CPU)\restore restore.cpp

$(CPU)\backup.obj: backup.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\backup /Fo$(CPU)\backup backup.cpp

$(CPU)\regsnap.obj: regsnap.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\regsnap /Fo$(CPU)\regsnap regsnap.cpp

$(CPU)\bfcopy.obj: bfcopy.cpp strarc.hpp
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(CPP_DEFINE) /Fp$(CPU)\bfcopy /Fo$(CPU)\bfcopy bfcopy.cpp

$(CPU)\lnk.obj: lnk.c lnk.h ..\include\winstrct.h Makefile
	cl /c $(WARNING_LEVEL) $(OPTIMIZATION) $(C_DEFINE) /Fp$(CPU)\lnk /Fo$(CPU)\lnk lnk.c

strarc.res: strarc.rc version.h Makefile
	rc strarc.rc

strarc.hpp: linktrack.hpp ..\include\ntfileio.hpp ..\include\spsleep.h ..\include\winstrct.hpp ..\include\winstrct.h Makefile

!IF "$(CPU)" == "i386"

install: p:\utils\strarc.exe p:\utils\strarc.txt z:\ltr-website\ltr-data.se\files\strarc.txt.gz z:\ltr-website\ltr-data.se\files\strarc.txt.gz Makefile

p:\utils\strarc.exe: i386\strarc.exe Makefile
	copy /y i386\strarc.exe p:\utils\ 

p:\utils\strarc.txt: strarc.txt Makefile
	copy /y strarc.txt p:\utils\ 

z:\ltr-website\ltr-data.se\files\strarc.txt: strarc.txt Makefile
	tr -d \r < strarc.txt > z:\ltr-website\ltr-data.se\files\strarc.txt

z:\ltr-website\ltr-data.se\files\strarc.txt.gz: strarc.txt Makefile
	tr -d \r < strarc.txt | gzip -9 > z:\ltr-website\ltr-data.se\files\strarc.txt.gz

!ELSEIF "$(CPU)" == "AMD64"

install: p:\utils64\strarc.exe p:\utils64\strarc.txt z:\ltr-website\ltr-data.se\files\strarc.txt.gz z:\ltr-website\ltr-data.se\files\strarc.txt Makefile

p:\utils64\strarc.exe: amd64\strarc.exe Makefile
	copy /y amd64\strarc.exe p:\utils64\ 

p:\utils64\strarc.txt: strarc.txt Makefile
	copy /y strarc.txt p:\utils64\ 

z:\ltr-website\ltr-data.se\files\strarc.txt: strarc.txt Makefile
	tr -d \r < strarc.txt > z:\ltr-website\ltr-data.se\files\strarc.txt

z:\ltr-website\ltr-data.se\files\strarc.txt.gz: strarc.txt Makefile
	tr -d \r < strarc.txt | gzip -9 > z:\ltr-website\ltr-data.se\files\strarc.txt.gz

!ENDIF

clean:
	del /s *.obj *~ *.pch
