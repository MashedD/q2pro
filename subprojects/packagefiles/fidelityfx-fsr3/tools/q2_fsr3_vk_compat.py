#!/usr/bin/env python3
"""Adapt the FSR3 Vulkan backend to 32-bit MinGW Vulkan handles."""

import pathlib
import sys


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise RuntimeError(f'expected one occurrence of {old!r}')
    return source.replace(old, new, 1)


source = pathlib.Path(sys.argv[1]).read_text(encoding='utf-8')
source = replace_once(
    source,
    '#include <vulkan/vulkan.h>',
    '#include <vulkan/vulkan.h>\n#include <cstdint>',
)
source = replace_once(
    source,
    'allocation.resource.resource = uniformBuffer;',
    '''#if VK_USE_64_BIT_PTR_DEFINES
    allocation.resource.resource = reinterpret_cast<void *>(uniformBuffer);
#else
    allocation.resource.resource = reinterpret_cast<void *>(static_cast<uintptr_t>(uniformBuffer));
#endif''',
)
source = replace_once(
    source,
    'static_cast<VkBuffer>(allocation.resource.resource)',
    'reinterpret_cast<VkBuffer>(allocation.resource.resource)',
)
source = replace_once(
    source,
    'vkCreateComputePipelines(backendContext->device, nullptr,',
    'vkCreateComputePipelines(backendContext->device, VK_NULL_HANDLE,',
)

source = replace_once(
    source,
    '''    return FFX_ALIGN_UP(sizeof(BackendContext_VK) + extensionPropArraySize + gpuJobDescArraySize + resourceViewArraySize + stagingRingBufferArraySize +
                            pipelineArraySize + resourceArraySize + contextArraySize,
                        sizeof(uint64_t));''',
    '''    const size_t contextAlignmentPadding = alignof(BackendContext_VK::EffectContext) - 1;
    return FFX_ALIGN_UP(sizeof(BackendContext_VK) + extensionPropArraySize + gpuJobDescArraySize + resourceViewArraySize + stagingRingBufferArraySize +
                            pipelineArraySize + resourceArraySize + contextArraySize + contextAlignmentPadding,
                        sizeof(uint64_t));''',
)

source = replace_once(
    source,
    '''        // Map context array
        backendContext->pEffectContexts = (BackendContext_VK::EffectContext*)pMem;''',
    '''        // Map context array. EffectContext is over-aligned, so align the
        // scratch pointer before constructing the array in-place.
        const std::uintptr_t contextAlignment = alignof(BackendContext_VK::EffectContext);
        const std::uintptr_t contextAddress =
            (reinterpret_cast<std::uintptr_t>(pMem) + contextAlignment - 1) &
            ~(contextAlignment - 1);
        pMem = reinterpret_cast<uint8_t *>(contextAddress);
        backendContext->pEffectContexts = (BackendContext_VK::EffectContext*)pMem;''',
)

pathlib.Path(sys.argv[2]).write_text(source, encoding='utf-8')
