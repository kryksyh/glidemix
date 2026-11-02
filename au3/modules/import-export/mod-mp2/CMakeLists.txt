#[[
MP2 export via twolame.
]]

set( TARGET au3-mod-mp2 )

set( SOURCES
   ExportMP2.cpp
   ExportMP2.h
)

set( LIBRARIES
PUBLIC
   twolame # ExportMP2.h includes twolame.h
   au3-files
   au3-import-export
   au3-mixer
   au3-tags
   au3-track
PRIVATE
   libid3tag::libid3tag
   wxBase
)

audacity_library( ${TARGET} "${SOURCES}" "${LIBRARIES}"
   "" ""
)

# ExportMP?.h changes with this define, so consumers need it too
target_compile_definitions( ${TARGET} PUBLIC USE_LIBID3TAG )
