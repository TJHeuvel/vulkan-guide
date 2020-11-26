---
layout: default
title: Gpu Driven Rendering DRAFT
parent: Gpu Driven Rendering
nav_order: 1
---

![city]({{site.baseurl}}/diagrams/cityrender.png)
Tutorial codebase, 125.000 objects processed and culled on main view and shadow view, 290 FPS. This view renders more than 40 million triangles due to the 2 mesh passes. RTX 2080



## GPU Driven Rendering

Over the last few years, the bleeding edge render engines have been moving more and more towards having the rendering itself calculated in gpu compute shaders.
Thanks to the apperance of MultiDrawIndirect and similar features, its now possible to do a very big amount of the work for the rendering inside compute shaders. The benefits are clear

* GPUs have orders of magnitude higher perf than CPU on data-parallel algorithms. Rendering is almost all data parallel algorithms
* With the GPU deciding its own work, latencies are minimized as there is no roundtrip from cpu to gpu and back.
* Frees the CPU from a lot of work, that can now be used on other things.

The result is an order of magnitude or more scene complexity and object counts. In the code that we will walk through in later chapters, based on the engine at the end of chapter 5 tutorial, we can run 250.000 "drawcalls", on Nintendo Switch, at more than 60 fps. On PC it reaches 500 fps. We essentially have a nearly unlimited object count, with the bottleneck moved into just how many triangles are we trying to make the gpu draw.

![perf]({{site.baseurl}}/diagrams/indirectperf.png)
CPU processing takes less than 0.5 miliseconds. Same view as above. Ryzen 1700


Techniques based on compute-shader-rendering have been becoming more popular in the last 5 years. Before that, they were used more on CAD type scenes.  Famously, Assassins Creed Unity and sequels use these techniques to achieve an order of magnitude more complex scenes, with Paris having an inmense amount of objects as its also rendering interiors for a lot of buildings. Frostbite engine from EA also uses these techniques to have very high geometry detail on Dragon Age Inquisition. These techniques are also the reason Rainbow Six Siege can have  thousands of dynamic rubble objects created from its destruction systems. The techniques have become very popular on the PS4 and Xbox One generation of consoles as they can get easily bottlenecked on triangle throughput, so having very accurate culling gives very great performance gains. Unreal Engine 4 and Unity do NOT use these techniques, but it looks like Unreal Engine 5 will use them.

The core of the idea revolves around the use of Draw Indirect support in the graphics APIs. These techniques work on all graphics APIs, but they work best on Vulkan or DX12 because they allow better control over low level memory management and compute barriers. They also work great on the Ps4 and Xbox One consoles, and the next generation has features that leans even more into it, such as Mesh Shaders and raytracing.

## Draw Indirect

Draw indirect is a drawcall that takes its parameters from a GPU buffer instead of from the call itself. When using draw indirect, you just start the draw based on a position on a gpu buffer, and then the GPU will execute the draw command in that buffer. 

Pseudocode:

```cpp

//normal drawing ---------------
vkCmdDrawIndexed(cmd, object.indexCount, 1 /* instance count */,object.firstIndex, object.vertexOffset, object.ID /* firstInstance */ );

//indirect drawing ---------------

Buffer* drawBuffer = create_buffer(sizeof(VkDrawIndexedIndirectCommand));

//we can inmediately enqueue the draw indirect
vkCmdDrawIndexedIndirect(cmd, drawBuffer.buffer, 0 /* offset */, 1 /* drawCount */, sizeof(VkDrawIndexedIndirectCommand));


//we can write the actual draw command at any time we want before VkQueueSubmit(), or from a different thread, or from a compute shader
VkDrawIndexedIndirectCommand* command = map_buffer(drawBuffer);

command->indexCount = object.indexCount;
command->instanceCount = 1;
command->firstIndex = object.firstIndex ;
command->vertexOffset = object.vertexOffset;
command->firstInstance = object.ID;
```

Because it takes its parameters from a buffer, its possible to use compute shaders to write into these buffers, and do culling or LOD selection in compute shaders. Doing culling this way is one of the simplest and most performant ways of doing culling. Due to the power of the GPU you can easily expect to cull more than a million objects in less than half a milisecond. Normal scenes dont tend to go as far. In more advanced pipelines like the one in Dragon Age or Rainbow Six, they go one step further and also cull individual triangles from the meshes. They do that by writing an output Index Buffer with the surviving triangles and using indirect to draw that.

When you design a gpu-driven renderer, the main idea is that all of the scene should be on the GPU. In chapter 4, we saw how to store the matrices for all loaded objects into a big SSBO. On GPU driven pipelines, we also want to store more data, such as material ID and cull bounds. Once we have a renderer where everything is stored in big GPU buffers, and we dont use PushConstants or descriptor sets per object, we are ready to go with a gpu-driven-renderer. The design of the tutorial engine is one that maps well into refactoring for a extreme performance compute based engine.

Due to the fact that you want to have as much things on the GPU as possible, this pipeline maps very well if you combine it with "Bindless" techniques, where you stop needing to bind descriptor sets per material or changing vertex buffers. In Doom Eternal engine, they go all-in on bindless, and the engine ends up doing very few drawcalls per frame. On this guide we will not use bindless textures as their support is limited, so we will do 1 draw-indirect call per material used. We will still merge all of the meshes into a big vertex buffer to avoid having to constantly bind it between draws. Having a bindless renderer also makes raytracing much more performant and effective. 

## Bindless Design

![perf]({{site.baseurl}}/diagrams/novus.png)
NovusCore Wow emulation research project. An entire continent from world of warcraft rendered in less than 10 drawcalls at 100+ FPS.

GPU driven pipelines work best when the amount of binds is as limited as possible. Best case scenario is to do a extremelly minimal amount of BindVertexBuffer, BindIndexBuffer, BindPipeline, and BindDescriptorSet calls. Bindless design makes cpu side work a lot faster due to the CPU having to do much less work, and the GPU can also go faster due to it being better utilized as each drawcall is "bigger". The less drawcalls you use to render your scene, the better, as modern GPUs are really big and have a big ramp up/ramp down time. Big modern GPUs love when you give them massive amounts of work on each drawcall, as that way they can ramp up to 100% usage.

To move vertex buffers/index buffers to bindless, generally you do it by merging the meshes into really big buffers. Instead of having 1 buffer per vertex buffer and index buffer pair, you have 1 buffer for all vertex buffers in a scene. When rendering, then you use BaseVertex offsets in the drawcalls.

In some engines, they remove vertex attributes from the pipelines enterely, and instead grab the vertex data from buffers in the vertex shader. Doing that lets you keep 1 big vertex buffer for all drawcalls in the engine even if they use different vertex formats much easily. It also allows some advanced unpacking/compression techniques, and its the main use case for Mesh Shaders.

To move textures into bindless, you use texture arrays. With the correct extension, the texture array can be unbounded in the shader, like when you use SSBOs. Then, when accessing the textures in the shader, you access them by index, which you grab from another buffer. If you dont have the Descriptor Indexing extensions, you can still use texture arrays, but they will need a bounded size. Check your device limits to see how big can that be.

To make materials bindless,you need to stop having 1 pipeline per material. Instead, you want to move the material parameters into SSBOs, and go with Ubershader approaches. In Doom engines, they have a very low amount of pipelines for the entire game, as they do that. Doom eternal has sub 500 pipelines, while Unreal Engine games often have 100.000+ pipelines. If you use ubershaders to massively lower the amount of unique pipelines, you will be able to increase efficiency in a huge way, as VkCmdBindPipeline is one of the most expensive calls when drawing objects in vulkan.

Push Constants and Dynamic Descriptors can be used, but they have to be "global". Using pushconstants for things like camera location is perfectly fine, but you cant use them for object ID as thats a per-object call and you specifically want to draw as many objects as possible in 1 draw.

The general workflow is to put stuff into buffers, and big buffers so you dont need to bind them every call. The bonus is that you can also write those buffers from the GPU, which is what Dragon Age Inquisition does for Index buffers, where it writes them from the culling shaders so that only the visible triangles will get drawn.


## Links 
* Assassins Creed Unity Engine + others: [PDF Link](https://www.advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf ) 
* Dragon Age Inquisition mesh culling: [GDC Link](https://www.gdcvault.com/play/1023109/Optimizing-the-Graphics-Pipeline-With)
* Rainbow Six Siege engine: [GDC Link](https://www.gdcvault.com/play/1022990/Rendering-Rainbow-Six-Siege)
* Doom Eternal engine: [Siggraph Link](https://advances.realtimerendering.com/s2020/RenderingDoomEternal.pdf)
* Nvidia Advanced Scenegraph:  [PDF Link](https://on-demand.gputechconf.com/gtc/2013/presentations/S3032-Advanced-Scenegraph-Rendering-Pipeline.pdf)


## Overview of Vkguide engine architecture for compute rendering.
The techniques here can be directly implemented after the 5 chapters of the core tutorial. The engine also has the things from Extra chapter implemented, but should be easy to follow.

The first thing is to go all in on object data in GPU buffers.Per-object PushConstants are removed, per-object dynamic uniform buffers are removed, and everything is replaced by ObjectBuffer where we store the object matrix and we index into it from the shader. 

We also change the way the meshes work. After loading a scene, we create a BIG vertex buffer, and stuff all of the meshes of the entire map into it. This way we will avoid having to rebind vertex buffers.

With the data management done, we can implement the indirect draw itself. 
<div class="mxgraph" style="max-width:100%;border:1px solid transparent;" data-mxgraph="{&quot;highlight&quot;:&quot;#0000ff&quot;,&quot;lightbox&quot;:false,&quot;nav&quot;:true,&quot;resize&quot;:true,&quot;toolbar&quot;:&quot;zoom layers&quot;,&quot;edit&quot;:&quot;_blank&quot;,&quot;xml&quot;:&quot;&lt;mxfile host=\&quot;app.diagrams.net\&quot; modified=\&quot;2020-11-26T19:58:22.731Z\&quot; agent=\&quot;5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/87.0.4280.66 Safari/537.36\&quot; etag=\&quot;PmBFAVOIx4z558Cg1Fsx\&quot; version=\&quot;13.10.4\&quot; type=\&quot;device\&quot;&gt;&lt;diagram id=\&quot;bKVgl3qcKdm3Ox6zxd8P\&quot; name=\&quot;Page-1\&quot;&gt;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&lt;/diagram&gt;&lt;/mxfile&gt;&quot;}"></div>
<script type="text/javascript" src="https://viewer.diagrams.net/js/viewer-static.min.js"></script>

The code that manages indirect draws is on the RenderScene class, which will grab all of the objects in a meshpass, and sort them into batches. A Batch is a set of objects that matches material and mesh. Each batch will be rendered with one DrawIndirect call that does instanced drawing. Each mesh pass (forward pass, shadow pass, others) is an array of batches. A given object can be on multiple mesh-passes at once.

In the main ObjectBuffer, we store the object matrix alongside the cull bounds per each object loaded in the engine. 

When starting the frame, we sync the objects that are on each mesh pass into a buffer. This buffer will be an array of ObjectID + BatchID. The BatchID maps directly as an index into the batch array of the mesh-pass.

Once we have that buffer uploaded and synced, we execute a compute shader that performs the culling.
For every object in said array of ObjectID + BatchID pairs, we access ObjectID in the ObjectBuffer, and check if its visible.
If its visible, we insert it into the Batches array, which contains the draw indirect calls, increasing the instance count. We also write it into the indirection buffer that maps from the instance ID of each batch into the ObjectID.

With that done, on the CPU side we execute 1 draw indirect call per batch in a mesh-pass. The gpu will use the parameters it just wrote into from the culling pass, and render everything.


{: .fs-6 .fw-300 }
{% include comments.html term="GPU Driven Rendering" %}