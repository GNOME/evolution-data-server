find_program(GETTEXT_XGETTEXT_EXECUTABLE xgettext)

set(DOMAIN ${FULL_PROJECT_NAME})
set(POT_FILE ${GETTEXT_PACKAGE}.pot)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/POTFILES.in TRANSLATED_FILES_1 ENCODING UTF-8 REGEX "^[^#].*")
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/POTFILES.source.in TRANSLATED_FILES_2 ENCODING UTF-8 REGEX "^[^#].*")
set(TRANSLATED_FILES)
foreach(TRANSLATED_FILE ${TRANSLATED_FILES_1} ${TRANSLATED_FILES_2})
    list(APPEND TRANSLATED_FILES "../${TRANSLATED_FILE}")
endforeach(TRANSLATED_FILE)

# Creates the .pot file containing the translations template
add_custom_command(
    COMMENT "Generating translation template"
    COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE} -o ${POT_FILE}
        --directory=${CMAKE_SOURCE_DIR}
        --package-name=${GETTEXT_PACKAGE}
        --files-from=${CMAKE_CURRENT_SOURCE_DIR}/POTFILES.in
        --from-code=UTF-8
        --add-comments
        --keyword=_
        --keyword=N_
        --keyword=C_:1c,2
        --keyword=NC_:1c,2
        --keyword=g_dcgettext:2
        --keyword=g_dngettext:2,3
        --keyword=g_dpgettext2:2c,3
        --flag=N_:1:pass-c-format
        --flag=C_:2:pass-c-format
        --flag=NC_:2:pass-c-format
        --flag=g_dngettext:2:pass-c-format
        --flag=g_strdup_printf:1:c-format
        --flag=g_string_printf:2:c-format
        --flag=g_string_append_printf:2:c-format
        --flag=g_error_new:3:c-format
        --flag=g_set_error:4:c-format
        --flag=g_markup_printf_escaped:1:c-format
        --flag=g_log:3:c-format
        --flag=g_print:1:c-format
        --flag=g_printerr:1:c-format
        --flag=g_printf:1:c-format
        --flag=g_fprintf:2:c-format
        --flag=g_sprintf:2:c-format
        --flag=g_snprintf:3:c-format
# .source files are not standard so handle them the best we can
    COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE} -o ${POT_FILE}
        --directory=${CMAKE_SOURCE_DIR}
        --package-name=${GETTEXT_PACKAGE}
        --language=Desktop
        --join-existing
        --files-from=${CMAKE_CURRENT_SOURCE_DIR}/POTFILES.source.in
        --from-code=UTF-8
        --add-comments
        --keyword=DisplayName
    OUTPUT ${POT_FILE}
    DEPENDS ${TRANSLATED_FILES}
)

file(GLOB po_files ${CMAKE_CURRENT_SOURCE_DIR}/*.po)
set(LINGUAS)
foreach(file IN LISTS po_files)
    get_filename_component(lang ${file} NAME_WE)
    list(APPEND LINGUAS ${lang})
    configure_file("${lang}.po" "${lang}.po" COPYONLY)
endforeach(file)

string (REPLACE ";" "\n" LINGUAS_CONTENT "${LINGUAS}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/LINGUAS" ${LINGUAS_CONTENT})
add_custom_target(linguas)
add_custom_command(TARGET linguas
    COMMENT "Updating LINGUAS"
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/LINGUAS ${CMAKE_CURRENT_SOURCE_DIR}/LINGUAS
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/LINGUAS
)

add_custom_target(${GETTEXT_PACKAGE}-pot DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${POT_FILE})

add_custom_target(${GETTEXT_PACKAGE}-update-po DEPENDS ${GETTEXT_PACKAGE}-pot)
foreach(LANG ${LINGUAS})
    add_custom_command(TARGET ${GETTEXT_PACKAGE}-update-po
        COMMENT "Update ${LANG}.po"
        COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE} -q --update ${CMAKE_CURRENT_SOURCE_DIR}/${LANG}.po ${CMAKE_CURRENT_BINARY_DIR}/${POT_FILE}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${POT_FILE}
    )
endforeach(LANG)

gettext_process_pot_file(${CMAKE_CURRENT_BINARY_DIR}/${POT_FILE} ALL INSTALL_DESTINATION "${LOCALE_INSTALL_DIR}" LANGUAGES ${LINGUAS})
