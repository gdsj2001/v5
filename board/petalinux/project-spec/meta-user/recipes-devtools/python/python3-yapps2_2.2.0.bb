SUMMARY = "Yapps2 parser runtime for LinuxCNC halcompile"
SECTION = "devel/python"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://yapps/runtime.py;beginline=1;endline=9;md5=f068b2dde144f37b98f4b91e9173b504"

SRC_URI = "https://files.pythonhosted.org/packages/7f/5d/f8b7dec89104f27d14b11711baf057194670ab220ebb8262f9bc1a450380/Yapps2-${PV}.tar.gz"
SRC_URI[sha256sum] = "fb5842d17177abc377e321edcc7349b1b903e4d883981784f27fa5bee1ab0091"

S = "${WORKDIR}/Yapps2-${PV}"

inherit setuptools3

BBCLASSEXTEND = "native"
