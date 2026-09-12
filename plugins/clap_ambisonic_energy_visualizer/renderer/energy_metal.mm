#include "energy_gpu.h"
#include "metal_shader.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstring>
#include <stdexcept>

namespace s3g::energy {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class MetalRenderer final : public Renderer {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> compute;
    id<MTLRenderPipelineState> draw;
    id<MTLBuffer> basisBuffer, weightsBuffer, snapshotsBuffer, fieldReadback, colorReadback;
    id<MTLTexture> fields[2], color;
    uint32_t index = 0, outputWidth = 0, outputHeight = 0;
    bool historyReady = false;
public:
    MetalRenderer(const std::vector<float>& basis, const Weights& weights) {
        device = MTLCreateSystemDefaultDevice();
        require(device != nil, "No Metal device available");
        queue = [device newCommandQueue];
        NSError* error = nil;
        // Identical source and default compile options to the shipping Mac GUI.
        id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:metalShader]
                                                    options:nil error:&error];
        if (!library) throw std::runtime_error(error.localizedDescription.UTF8String);
        compute = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"analysisMain"] error:&error];
        if (!compute) throw std::runtime_error(error.localizedDescription.UTF8String);
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [library newFunctionWithName:@"vertexMain"];
        desc.fragmentFunction = [library newFunctionWithName:@"fragmentMain"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        draw = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!draw) throw std::runtime_error(error.localizedDescription.UTF8String);
        basisBuffer = [device newBufferWithBytes:basis.data() length:basis.size() * sizeof(float)
                                        options:MTLResourceStorageModeShared];
        weightsBuffer = [device newBufferWithBytes:weights.data() length:sizeof(weights)
                                          options:MTLResourceStorageModeShared];
        snapshotsBuffer = [device newBufferWithLength:sizeof(Snapshots) options:MTLResourceStorageModeShared];
        fieldReadback = [device newBufferWithLength:pixels * 8 options:MTLResourceStorageModeShared];
        MTLTextureDescriptor* texture = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                        width:columns height:rows mipmapped:NO];
        texture.storageMode = MTLStorageModePrivate;
        texture.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        fields[0] = [device newTextureWithDescriptor:texture];
        fields[1] = [device newTextureWithDescriptor:texture];
        require(queue && basisBuffer && weightsBuffer && snapshotsBuffer && fieldReadback && fields[0] && fields[1],
                "Metal resource allocation failed");
    }
    std::string deviceName() const override { return std::string("Metal: ") + device.name.UTF8String; }
    Capture render(const Params& p, const Snapshots& snapshots, uint32_t width, uint32_t height) override {
        @autoreleasepool {
            validate(p, snapshots, width, height);
            require(historyReady || p.resetHistory, "First frame must reset history");
            const size_t colorPitch = (size_t(width) * 4 + 255) & ~size_t(255);
            if (width != outputWidth || height != outputHeight) {
                MTLTextureDescriptor* texture = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                    width:width height:height mipmapped:NO];
                texture.storageMode = MTLStorageModePrivate;
                texture.usage = MTLTextureUsageRenderTarget;
                color = [device newTextureWithDescriptor:texture];
                colorReadback = [device newBufferWithLength:colorPitch * height options:MTLResourceStorageModeShared];
                require(color && colorReadback, "Metal output allocation failed");
                outputWidth = width; outputHeight = height;
            }
            std::memcpy(snapshotsBuffer.contents, snapshots.data(), sizeof(snapshots));
            const uint32_t next = index ^ 1;
            id<MTLCommandBuffer> command = [queue commandBuffer];
            require(command != nil, "Metal command buffer unavailable");
            id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
            require(encoder != nil, "Metal compute encoder unavailable");
            [encoder setComputePipelineState:compute];
            [encoder setBuffer:basisBuffer offset:0 atIndex:0];
            [encoder setBuffer:snapshotsBuffer offset:0 atIndex:1];
            [encoder setBytes:&p length:sizeof(p) atIndex:2];
            [encoder setBuffer:weightsBuffer offset:0 atIndex:3];
            [encoder setTexture:fields[index] atIndex:0];
            [encoder setTexture:fields[next] atIndex:1];
            [encoder dispatchThreads:MTLSizeMake(columns, rows, 1) threadsPerThreadgroup:MTLSizeMake(16, 8, 1)];
            [encoder endEncoding];
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = color;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(.02, .02, .02, 1);
            id<MTLRenderCommandEncoder> painter = [command renderCommandEncoderWithDescriptor:pass];
            require(painter != nil, "Metal render encoder unavailable");
            [painter setRenderPipelineState:draw];
            [painter setViewport:MTLViewport{0, 0, double(width), double(height), 0, 1}];
            [painter setFragmentTexture:fields[next] atIndex:0];
            [painter setFragmentBytes:&p length:sizeof(p) atIndex:0];
            [painter drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [painter endEncoding];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            require(blit != nil, "Metal readback encoder unavailable");
            [blit copyFromTexture:fields[next] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                       sourceSize:MTLSizeMake(columns,rows,1) toBuffer:fieldReadback destinationOffset:0
          destinationBytesPerRow:columns * 8 destinationBytesPerImage:pixels * 8];
            [blit copyFromTexture:color sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                       sourceSize:MTLSizeMake(width,height,1) toBuffer:colorReadback destinationOffset:0
          destinationBytesPerRow:colorPitch destinationBytesPerImage:colorPitch * height];
            [blit endEncoding];
            [command commit];
            [command waitUntilCompleted];
            if (command.status == MTLCommandBufferStatusError)
                throw std::runtime_error(command.error.localizedDescription.UTF8String);
            Capture result;
            result.width = width; result.height = height;
            result.field.resize(pixels * 4); result.rgba.resize(size_t(width) * height * 4);
            const auto* half = static_cast<const uint16_t*>(fieldReadback.contents);
            for (size_t i = 0; i < result.field.size(); ++i) result.field[i] = fromHalf(half[i]);
            for (uint32_t y = 0; y < height; ++y)
                std::memcpy(result.rgba.data() + size_t(y) * width * 4,
                    static_cast<const uint8_t*>(colorReadback.contents) + y * colorPitch, width * 4);
            index = next; historyReady = true;
            return result;
        }
    }
};
}
std::unique_ptr<Renderer> makeRenderer(const std::vector<float>& basis, const Weights& weights, bool software) {
    if (software) throw std::runtime_error("WARP is a Windows-only comparison mode");
    if (basis.size() != pixels * channels) throw std::runtime_error("Invalid basis size");
    return std::make_unique<MetalRenderer>(basis, weights);
}
}
