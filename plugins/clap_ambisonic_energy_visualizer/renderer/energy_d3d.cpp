#define NOMINMAX
#include "energy_gpu.h"
#include "d3d_shader.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace s3g::energy {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream message;
        message << operation << " failed (0x" << std::hex << uint32_t(hr) << ")";
        throw std::runtime_error(message.str());
    }
}
ComPtr<ID3DBlob> compile(const char* entry, const char* profile) {
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(d3dShader, std::strlen(d3dShader), "ambi-energy.hlsl", nullptr,
        nullptr, entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &code, &errors);
    if (FAILED(hr) && errors) throw std::runtime_error(std::string("HLSL ") + entry + ": " +
        std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    check(hr, "D3DCompile");
    return code;
}
class D3DRenderer final : public Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11ComputeShader> compute;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> basisBuffer, weightsBuffer, snapshotsBuffer, constants;
    ComPtr<ID3D11ShaderResourceView> basisView, weightsView, snapshotsView, fieldView[2];
    ComPtr<ID3D11UnorderedAccessView> fieldWrite[2];
    ComPtr<ID3D11Texture2D> fields[2], fieldReadback, color, colorReadback;
    ComPtr<ID3D11RenderTargetView> colorTarget;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RasterizerState> rasterizer;
    uint32_t index = 0, outputWidth = 0, outputHeight = 0;
    bool historyReady = false;
    std::string name;
    void buffer(const float* data, UINT count,
                ComPtr<ID3D11Buffer>& storage, ComPtr<ID3D11ShaderResourceView>& view) {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = count * sizeof(float);
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(float);
        D3D11_SUBRESOURCE_DATA initial{}; initial.pSysMem = data;
        check(device->CreateBuffer(&desc, data ? &initial : nullptr, &storage), "Create structured buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements = count;
        check(device->CreateShaderResourceView(storage.Get(), &srv, &view), "Create buffer SRV");
    }
    void upload(ID3D11Buffer* buffer, const void* data, size_t size) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map upload buffer");
        std::memcpy(mapped.pData, data, size);
        context->Unmap(buffer, 0);
    }
public:
    D3DRenderer(const std::vector<float>& basis, const Weights& weights, bool software) {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL level{};
        check(D3D11CreateDevice(nullptr, software ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, &level, &context),
            "Create D3D11 feature-level 11_0 device");
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC adapterDesc{};
        check(device.As(&dxgi), "Query DXGI device");
        check(dxgi->GetAdapter(&adapter), "Get adapter");
        check(adapter->GetDesc(&adapterDesc), "Get adapter description");
        char utf8[512]{};
        if (!WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, utf8, sizeof(utf8), nullptr, nullptr))
            throw std::runtime_error("Adapter name UTF-8 conversion failed");
        name = std::string(software ? "D3D11 WARP: " : "D3D11 hardware: ") + utf8;
        auto cs = compile("analysisMain", "cs_5_0");
        auto vs = compile("vertexMain", "vs_5_0");
        auto ps = compile("fragmentMain", "ps_5_0");
        check(device->CreateComputeShader(cs->GetBufferPointer(), cs->GetBufferSize(), nullptr, &compute), "Create CS");
        check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertex), "Create VS");
        check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixel), "Create PS");
        buffer(basis.data(), UINT(basis.size()), basisBuffer, basisView);
        buffer(weights.data(), UINT(weights.size()), weightsBuffer, weightsView);
        // D3D11 does not allow CPU access flags on structured buffers. Snapshot
        // updates use a DEFAULT buffer and UpdateSubresource, outside audio.
        D3D11_BUFFER_DESC snapshotDesc{};
        snapshotDesc.ByteWidth = sizeof(Snapshots);
        snapshotDesc.Usage = D3D11_USAGE_DEFAULT;
        snapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        snapshotDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        snapshotDesc.StructureByteStride = sizeof(float);
        check(device->CreateBuffer(&snapshotDesc, nullptr, &snapshotsBuffer), "Create snapshot buffer");
        D3D11_SHADER_RESOURCE_VIEW_DESC snapshotSRV{};
        snapshotSRV.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        snapshotSRV.Buffer.NumElements = channels * snapshotLimit;
        check(device->CreateShaderResourceView(snapshotsBuffer.Get(), &snapshotSRV, &snapshotsView), "Create snapshot SRV");
        D3D11_BUFFER_DESC constantDesc{};
        constantDesc.ByteWidth = sizeof(Params);
        constantDesc.Usage = D3D11_USAGE_DYNAMIC;
        constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&constantDesc, nullptr, &constants), "Create constants");
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = columns; texture.Height = rows; texture.MipLevels = texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        texture.SampleDesc.Count = 1;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        for (unsigned i = 0; i < 2; ++i) {
            check(device->CreateTexture2D(&texture, nullptr, &fields[i]), "Create field texture");
            check(device->CreateShaderResourceView(fields[i].Get(), nullptr, &fieldView[i]), "Create field SRV");
            check(device->CreateUnorderedAccessView(fields[i].Get(), nullptr, &fieldWrite[i]), "Create field UAV");
            const float zero[4]{};
            context->ClearUnorderedAccessViewFloat(fieldWrite[i].Get(), zero);
        }
        texture.BindFlags = 0; texture.Usage = D3D11_USAGE_STAGING; texture.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&texture, nullptr, &fieldReadback), "Create field staging texture");
        D3D11_SAMPLER_DESC sample{};
        sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxLOD = D3D11_FLOAT32_MAX;
        sample.ComparisonFunc = D3D11_COMPARISON_NEVER;
        check(device->CreateSamplerState(&sample, &sampler), "Create linear-clamp sampler");
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, &rasterizer), "Create rasterizer");
    }
    std::string deviceName() const override { return name; }
    Capture render(const Params& p, const Snapshots& samples, uint32_t width, uint32_t height) override {
        validate(p, samples, width, height);
        if (!historyReady && !p.resetHistory) throw std::runtime_error("First frame must reset history");
        if (width != outputWidth || height != outputHeight) {
            color.Reset(); colorTarget.Reset(); colorReadback.Reset();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            check(device->CreateTexture2D(&desc, nullptr, &color), "Create color texture");
            check(device->CreateRenderTargetView(color.Get(), nullptr, &colorTarget), "Create color target");
            desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(device->CreateTexture2D(&desc, nullptr, &colorReadback), "Create color staging texture");
            outputWidth = width; outputHeight = height;
        }
        upload(constants.Get(), &p, sizeof(p));
        context->UpdateSubresource(snapshotsBuffer.Get(), 0, nullptr, samples.data(), 0, 0);
        const uint32_t next = index ^ 1;
        ID3D11ShaderResourceView* resources[] = {basisView.Get(), snapshotsView.Get(), weightsView.Get(), fieldView[index].Get()};
        ID3D11Buffer* cb = constants.Get();
        ID3D11UnorderedAccessView* uav = fieldWrite[next].Get();
        context->CSSetShader(compute.Get(), nullptr, 0);
        context->CSSetConstantBuffers(0, 1, &cb);
        context->CSSetShaderResources(0, 4, resources);
        context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        context->Dispatch((columns + 15) / 16, (rows + 7) / 8, 1);
        // Unbind UAV before sampling it; unbind all SRVs before the next swap.
        ID3D11UnorderedAccessView* noUAV = nullptr;
        ID3D11ShaderResourceView* noSRVs[4]{};
        context->CSSetUnorderedAccessViews(0, 1, &noUAV, nullptr);
        context->CSSetShaderResources(0, 4, noSRVs);
        ID3D11RenderTargetView* target = colorTarget.Get();
        const float clear[] = {.02f, .02f, .02f, 1};
        context->ClearRenderTargetView(target, clear);
        context->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(rasterizer.Get());
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vertex.Get(), nullptr, 0);
        context->PSSetShader(pixel.Get(), nullptr, 0);
        context->PSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* field = fieldView[next].Get();
        ID3D11SamplerState* sample = sampler.Get();
        context->PSSetShaderResources(4, 1, &field);
        context->PSSetSamplers(0, 1, &sample);
        context->Draw(4, 0);
        context->PSSetShaderResources(4, 1, noSRVs);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->CopyResource(fieldReadback.Get(), fields[next].Get());
        context->CopyResource(colorReadback.Get(), color.Get());
        Capture result;
        result.width = width; result.height = height;
        result.field.resize(pixels * 4); result.rgba.resize(size_t(width) * height * 4);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(fieldReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read back field");
        for (uint32_t y = 0; y < rows; ++y) {
            const auto* half = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch);
            for (uint32_t x = 0; x < columns * 4; ++x) result.field[y * columns * 4 + x] = fromHalf(half[x]);
        }
        context->Unmap(fieldReadback.Get(), 0);
        check(context->Map(colorReadback.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read back color");
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(result.rgba.data() + size_t(y) * width * 4,
                        static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch, width * 4);
        context->Unmap(colorReadback.Get(), 0);
        check(device->GetDeviceRemovedReason(), "D3D11 device status");
        index = next; historyReady = true;
        return result;
    }
};
}
std::unique_ptr<Renderer> makeRenderer(const std::vector<float>& basis, const Weights& weights, bool software) {
    if (basis.size() != pixels * channels) throw std::runtime_error("Invalid basis size");
    return std::make_unique<D3DRenderer>(basis, weights, software);
}
}
