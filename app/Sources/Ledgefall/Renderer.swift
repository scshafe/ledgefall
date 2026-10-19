import LedgefallKit
import Metal
import MetalKit
import QuartzCore

// Flat-coloured quads, drawn as two triangles each. The shader is compiled
// from source at start-up so the package needs no .metal build step.
private let shaderSource = """
#include <metal_stdlib>
using namespace metal;

struct Vertex {
    float2 position;
    float4 color;
};

struct Uniforms {
    float2 scale;
    float2 offset;
};

struct Out {
    float4 position [[position]];
    float4 color;
};

vertex Out quad_vertex(uint id [[vertex_id]],
                       constant Vertex* vertices [[buffer(0)]],
                       constant Uniforms& u [[buffer(1)]]) {
    Out out;
    out.position = float4(vertices[id].position * u.scale + u.offset, 0.0, 1.0);
    out.color = vertices[id].color;
    return out;
}

fragment float4 quad_fragment(Out in [[stage_in]]) {
    return in.color;
}
"""

private struct Vertex {
    var position: SIMD2<Float>
    var color: SIMD4<Float>
}

private struct Uniforms {
    var scale: SIMD2<Float>
    var offset: SIMD2<Float>
}

final class Renderer: NSObject, MTKViewDelegate {
    private let queue: MTLCommandQueue
    private let pipeline: MTLRenderPipelineState
    private let device: MTLDevice
    private let model: AppModel

    init?(view: MTKView, model: AppModel) {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else { return nil }
        self.device = device
        self.queue = queue
        self.model = model

        do {
            let library = try device.makeLibrary(source: shaderSource, options: nil)
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.vertexFunction = library.makeFunction(name: "quad_vertex")
            descriptor.fragmentFunction = library.makeFunction(name: "quad_fragment")
            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
        } catch {
            NSLog("ledgefall: Metal setup failed: \(error)")
            return nil
        }
        super.init()
        view.device = device
        view.clearColor = MTLClearColor(red: 0.07, green: 0.08, blue: 0.12, alpha: 1)
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        model.frame(now: CACurrentMediaTime())

        guard let drawable = view.currentDrawable,
              let pass = view.currentRenderPassDescriptor,
              let commands = queue.makeCommandBuffer(),
              let encoder = commands.makeRenderCommandEncoder(descriptor: pass) else { return }

        let match = model.loop.match
        let size = view.drawableSize
        let camera = Camera(blast: match.blast, viewWidth: Float(size.width), viewHeight: Float(size.height))
        var uniforms = Uniforms(scale: camera.scale, offset: camera.offset)

        var vertices: [Vertex] = []
        for q in Scene.quads(for: match) {
            let a = SIMD2<Float>(q.left, q.bottom), b = SIMD2<Float>(q.right, q.bottom)
            let c = SIMD2<Float>(q.right, q.top), d = SIMD2<Float>(q.left, q.top)
            for p in [a, b, c, a, c, d] { vertices.append(Vertex(position: p, color: q.color)) }
        }

        if !vertices.isEmpty,
           let buffer = device.makeBuffer(bytes: vertices, length: MemoryLayout<Vertex>.stride * vertices.count, options: []) {
            encoder.setRenderPipelineState(pipeline)
            encoder.setVertexBuffer(buffer, offset: 0, index: 0)
            encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
            encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: vertices.count)
        }
        encoder.endEncoding()
        commands.present(drawable)
        commands.commit()
    }
}
