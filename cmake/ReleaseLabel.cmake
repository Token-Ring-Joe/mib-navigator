# The single source of the release label. Edit ONLY this file to change the version.
#
#   SNMPB_BASE_VERSION    major.minor.patch
#   SNMPB_RELEASE_SUFFIX  "rc2", "beta1", ... or empty for a final release
#
# Everything else reads the result: the project version, the package and archive names,
# the About box, the archive verification scripts and the release workflow (which also
# refuses a tag that does not match). To release: change the suffix here, commit, then
# tag it. The tag for suffix "rc2" is "v1.0.0-rc.2".
set(SNMPB_BASE_VERSION "1.0.0")
set(SNMPB_RELEASE_SUFFIX "rc2")

if(SNMPB_RELEASE_SUFFIX)
    set(SNMPB_RELEASE_LABEL "${SNMPB_BASE_VERSION}-${SNMPB_RELEASE_SUFFIX}")
else()
    set(SNMPB_RELEASE_LABEL "${SNMPB_BASE_VERSION}")
endif()
