# ******************************************************************************
#  modules_doc.pri                                                  Tao project
# ******************************************************************************
# File Description:
# Common project include file to build documentation for Tao modules
# ******************************************************************************
# This software is property of Taodyne SAS - Confidential
# Ce logiciel est la propriété de Taodyne SAS - Confidentiel
# (C) 2011 Jerome Forissier <jerome@taodyne.com>
# (C) 2011 Taodyne SAS
# ******************************************************************************

include(modules_defs.pri)

!isEmpty(DOXYFILE) {

  system(bash -c \"doxygen --version >/dev/null 2>&1\"):HAS_DOXYGEN=1

  equals(HAS_DOXYGEN, 1) {

    DOC_DIR = $$dirname(DOXYFILE)
    DOXY = $$basename(DOXYFILE)
    moddoc.target = doc
    moddoc.commands = (cd $$DOC_DIR ; doxygen $$DOXY)
    moddoc.depends = FORCE
    QMAKE_EXTRA_TARGETS += moddoc

    htmldocinstall.path = $${MODINSTPATH}/doc
    htmldocinstall.commands = mkdir -p \"$$MODINSTPATH/doc\" ; cp -R $$DOC_DIR/html \"$$MODINSTPATH/doc\"
    htmldocinstall.depends = moddoc
    QMAKE_EXTRA_TARGETS += htmldocinstall
    INSTALLS += htmldocinstall

    install_qch.path = $${MODINSTPATH}/qch
    install_qch.commands = mkdir -p \"$$MODINSTPATH/doc/qch\" ; cp $$DOC_DIR/qch/*.qch \"$$MODINSTPATH/doc/qch/\"
    install_qch.depends = moddoc
    QMAKE_EXTRA_TARGETS += install_qch
    INSTALLS += install_qch

    docclean.commands = rm -rf $$DOC_DIR/html $$DOC_DIR/qch
    distclean.depends = docclean
    QMAKE_EXTRA_TARGETS += docclean distclean

  } else {

    warning(doxygen not found - will not build online documentation)

  }
}
