# ─────────────────────────────────────────────────────────────────────────────
# Đường dẫn component dùng chung cho MỌI example — sửa đúng MỘT chỗ này.
#
#   components/     — SDK hạ tầng
#   components-hw/  — driver phần cứng mẫu (KHÔNG thuộc SDK; example 06/08 dùng)
#
# Example cần driver phần cứng thì set IE_WANT_HW TRƯỚC khi include file này.
# ─────────────────────────────────────────────────────────────────────────────
get_filename_component(IE_EXAMPLES_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(IE_SDK_ROOT "${IE_EXAMPLES_DIR}/.." ABSOLUTE)

list(APPEND EXTRA_COMPONENT_DIRS "${IE_SDK_ROOT}/components")
if(IE_WANT_HW)
    list(APPEND EXTRA_COMPONENT_DIRS "${IE_SDK_ROOT}/components-hw")
endif()

# sdkconfig chung + sdkconfig riêng của example (nếu có).
set(SDKCONFIG_DEFAULTS "${IE_EXAMPLES_DIR}/sdkconfig.defaults")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
    list(APPEND SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults")
endif()
