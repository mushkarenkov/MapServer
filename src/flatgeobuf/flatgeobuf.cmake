list(APPEND FLATGEOBUF_SRC
    ${CMAKE_CURRENT_LIST_DIR}/feature_generated.h
    ${CMAKE_CURRENT_LIST_DIR}/flatgeobuf_c.cpp ${CMAKE_CURRENT_LIST_DIR}/flatgeobuf_c.h
    ${CMAKE_CURRENT_LIST_DIR}/geometryreader.cpp ${CMAKE_CURRENT_LIST_DIR}/geometryreader.h
    ${CMAKE_CURRENT_LIST_DIR}/header_generated.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/allocator.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/array.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/base.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/bfbs_generator.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/buffer.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/buffer_ref.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/code_generators.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/default_allocator.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/detached_buffer.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/flatbuffer_builder.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/flatbuffers.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/flatc.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/flex_flat_util.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/flexbuffers.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/grpc.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/hash.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/idl.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/minireflect.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/pch/flatc_pch.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/pch/pch.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/reflection.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/reflection_generated.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/registry.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/stl_emulation.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/string.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/struct.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/table.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/util.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/vector.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/vector_downward.h
    ${CMAKE_CURRENT_LIST_DIR}/include/flatbuffers/verifier.h
    ${CMAKE_CURRENT_LIST_DIR}/packedrtree.cpp ${CMAKE_CURRENT_LIST_DIR}/packedrtree.h
)
