/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "GLMapRenderer.h"
#include "GLDynamicLightShader.h"
#include "GLImage.h"
#include "GLMapChunk.h"
#include "GLMapShadowRenderer.h"
#include "GLProfiler.h"
#include "GLProgram.h"
#include "GLProgramAttribute.h"
#include "GLProgramUniform.h"
#include "GLRenderer.h"
#include "GLShadowShader.h"
#include "IGLDevice.h"
#include <Client/GameMap.h>
#include <Core/Debug.h>
#include <Core/Settings.h>

namespace spades {
	namespace draw {
		void GLMapRenderer::PreloadShaders(GLRenderer& renderer) {
			if (renderer.GetSettings().r_physicalLighting)
				renderer.RegisterProgram("Shaders/BasicBlockPhys.program");
			else
				renderer.RegisterProgram("Shaders/BasicBlock.program");
			renderer.RegisterProgram("Shaders/BasicBlockDepthOnly.program");
			renderer.RegisterProgram("Shaders/BasicBlockDynamicLit.program");
			renderer.RegisterProgram("Shaders/BackFaceBlock.program");
			renderer.RegisterImage("Gfx/AmbientOcclusion.png");
		}

		GLMapRenderer::GLMapRenderer(client::GameMap* m, GLRenderer& r)
		    : renderer(r), device(r.GetGLDevice()), gameMap(m) {
			SPADES_MARK_FUNCTION();

			numChunkWidth = gameMap->Width() / GLMapChunk::Size;
			numChunkHeight = gameMap->Height() / GLMapChunk::Size;
			numChunkDepth = gameMap->Depth() / GLMapChunk::Size;

			numChunks = numChunkWidth * numChunkHeight * numChunkDepth;

			chunks = new GLMapChunk*[numChunks];
			chunkInfos = new ChunkRenderInfo[numChunks];

			for (int i = 0; i < numChunks; i++)
				chunks[i] = new GLMapChunk(*this, gameMap, i / numChunkDepth / numChunkHeight,
				                           (i / numChunkDepth) % numChunkHeight, i % numChunkDepth);

			if (r.GetSettings().r_physicalLighting)
				basicProgram = renderer.RegisterProgram("Shaders/BasicBlockPhys.program");
			else
				basicProgram = renderer.RegisterProgram("Shaders/BasicBlock.program");
			depthonlyProgram = renderer.RegisterProgram("Shaders/BasicBlockDepthOnly.program");
			dlightProgram = renderer.RegisterProgram("Shaders/BasicBlockDynamicLit.program");
			backfaceProgram = renderer.RegisterProgram("Shaders/BackFaceBlock.program");
			aoImage = renderer.RegisterImage("Gfx/AmbientOcclusion.png").Cast<GLImage>();

			static const uint8_t squareVertices[] = {0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 1};
			squareVertexBuffer = device.GenBuffer();
			device.BindBuffer(IGLDevice::ArrayBuffer, squareVertexBuffer);
			device.BufferData(IGLDevice::ArrayBuffer, sizeof(squareVertices), squareVertices,
			                  IGLDevice::StaticDraw);
			device.BindBuffer(IGLDevice::ArrayBuffer, 0);
		}

		GLMapRenderer::~GLMapRenderer() {
			SPADES_MARK_FUNCTION();

			device.DeleteBuffer(squareVertexBuffer);
			for (int i = 0; i < numChunks; i++)
				delete chunks[i];
			delete[] chunks;
			delete[] chunkInfos;
		}
		void GLMapRenderer::GameMapChanged(int x, int y, int z, client::GameMap* map) {
			SPADES_MARK_FUNCTION_DEBUG();

			int fz = z & (GLMapChunk::Size - 1);
			int sx = -1;
			int sy = -1;
			int sz = (fz == 0) ? -1 : 0;
			int ex = 1;
			int ey = 1;
			int ez = (fz == (GLMapChunk::Size - 1)) ? 1 : 0;
			for (int cx = sx; cx <= ex; cx++)
			for (int cy = sy; cy <= ey; cy++)
			for (int cz = sz; cz <= ez; cz++) {
				int xx = x + cx, yy = y + cy, zz = z + cz;
				xx >>= GLMapChunk::SizeBits;
				yy >>= GLMapChunk::SizeBits;
				zz >>= GLMapChunk::SizeBits;
				xx &= numChunkWidth - 1;
				yy &= numChunkHeight - 1;
				if (xx >= 0 && yy >= 0 && zz >= 0 && xx < numChunkWidth &&
					yy < numChunkHeight && zz < numChunkDepth)
					GetChunk(xx, yy, zz)->SetNeedsUpdate();
			}
		}

		void GLMapRenderer::RealizeChunks(spades::Vector3 eye) {
			SPADES_MARK_FUNCTION();

			float cullDistance = 128.0F;
			float releaseDistance = cullDistance + 32.0F;
			for (int i = 0; i < numChunks; i++) {
				float dist = chunks[i]->DistanceFromEye(eye);
				chunkInfos[i].distance = dist;
				if (dist < cullDistance)
					chunks[i]->SetRealized(true);
				else if (dist > releaseDistance)
					chunks[i]->SetRealized(false);
			}
		}

		void GLMapRenderer::Realize() {
			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Map Chunks");
			RealizeChunks(renderer.GetSceneDef().viewOrigin);
		}

		void GLMapRenderer::Prerender() {
			SPADES_MARK_FUNCTION();
			// depth-only pass

			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Map");
			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);
			device.ColorMask(false, false, false, false);

			depthonlyProgram->Use();
			static GLProgramAttribute positionAttribute("positionAttribute");
			positionAttribute(depthonlyProgram);
			device.EnableVertexAttribArray(positionAttribute(), true);
			static GLProgramUniform projectionViewMatrix("projectionViewMatrix");
			projectionViewMatrix(depthonlyProgram);
			projectionViewMatrix.SetValue(renderer.GetProjectionViewMatrix());

			// draw from nearest to farthest
			IntVector3 c = viewOrigin.Floor() / GLMapChunk::Size;
			DrawColumnDepth(c.x, c.y, c.z, viewOrigin);
			for (int dist = 1; dist <= 128 / GLMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					DrawColumnDepth(x, c.y + dist, c.z, viewOrigin);
					DrawColumnDepth(x, c.y - dist, c.z, viewOrigin);
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					DrawColumnDepth(c.x + dist, y, c.z, viewOrigin);
					DrawColumnDepth(c.x - dist, y, c.z, viewOrigin);
				}
			}

			device.EnableVertexAttribArray(positionAttribute(), false);
			device.ColorMask(true, true, true, true);
		}

		void GLMapRenderer::RenderSunlightPass() {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Map");
			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;

			// draw back face to avoid cheating.
			// without this, players can see through blocks by
			// covering themselves by ones.
			RenderBackface();

			device.ActiveTexture(0);
			aoImage->Bind(IGLDevice::Texture2D);
			device.TexParamater(IGLDevice::Texture2D,
				IGLDevice::TextureMinFilter, IGLDevice::Linear);

			device.ActiveTexture(1);
			device.BindTexture(IGLDevice::Texture2D, 0);

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);

			basicProgram->Use();

			static GLShadowShader shadowShader;
			shadowShader(&renderer, basicProgram, 2);

			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(basicProgram);
			fogDistance.SetValue(renderer.GetFogDistance());

			static GLProgramUniform viewSpaceLight("viewSpaceLight");
			viewSpaceLight(basicProgram);
			Vector3 vspLight = (renderer.GetViewMatrix() * MakeVector4(0, -1, -1, 0)).GetXYZ();
			viewSpaceLight.SetValue(vspLight.x, vspLight.y, vspLight.z);

			static GLProgramUniform fogColor("fogColor");
			fogColor(basicProgram);
			Vector3 fogCol = renderer.GetFogColorForSolidPass();
			fogCol *= fogCol; // linearize
			fogColor.SetValue(fogCol.x, fogCol.y, fogCol.z);

			static GLProgramUniform aoUniform("ambientOcclusionTexture");
			aoUniform(basicProgram);
			aoUniform.SetValue(0);

			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute ambientOcclusionCoordAttribute("ambientOcclusionCoordAttribute");
			static GLProgramAttribute colorAttribute("colorAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");
			static GLProgramAttribute fixedPositionAttribute("fixedPositionAttribute");

			positionAttribute(basicProgram);
			ambientOcclusionCoordAttribute(basicProgram);
			colorAttribute(basicProgram);
			normalAttribute(basicProgram);
			fixedPositionAttribute(basicProgram);

			device.EnableVertexAttribArray(positionAttribute(), true);
			if (ambientOcclusionCoordAttribute() != -1)
				device.EnableVertexAttribArray(ambientOcclusionCoordAttribute(), true);
			device.EnableVertexAttribArray(colorAttribute(), true);
			if (normalAttribute() != -1)
				device.EnableVertexAttribArray(normalAttribute(), true);
			device.EnableVertexAttribArray(fixedPositionAttribute(), true);

			static GLProgramUniform projectionViewMatrix("projectionViewMatrix");
			projectionViewMatrix(basicProgram);
			projectionViewMatrix.SetValue(renderer.GetProjectionViewMatrix());

			static GLProgramUniform viewMatrix("viewMatrix");
			viewMatrix(basicProgram);
			viewMatrix.SetValue(renderer.GetViewMatrix());

			static GLProgramUniform viewOriginVector("viewOriginVector");
			viewOriginVector(basicProgram);
			viewOriginVector.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);

			// RealizeChunks(eye); // should already be realized from the prepass
			// TODO maybe add some way of checking if the chunks have been realized for the current
			// eye? Probably just a bool called "alreadyrealized" that gets checked in RealizeChunks

			// draw from nearest to farthest
			IntVector3 c = viewOrigin.Floor() / GLMapChunk::Size;
			DrawColumnSunlight(c.x, c.y, c.z, viewOrigin);
			for (int dist = 1; dist <= 128 / GLMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					DrawColumnSunlight(x, c.y + dist, c.z, viewOrigin);
					DrawColumnSunlight(x, c.y - dist, c.z, viewOrigin);
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					DrawColumnSunlight(c.x + dist, y, c.z, viewOrigin);
					DrawColumnSunlight(c.x - dist, y, c.z, viewOrigin);
				}
			}

			device.EnableVertexAttribArray(positionAttribute(), false);
			if (ambientOcclusionCoordAttribute() != -1)
				device.EnableVertexAttribArray(ambientOcclusionCoordAttribute(), false);
			device.EnableVertexAttribArray(colorAttribute(), false);
			if (normalAttribute() != -1)
				device.EnableVertexAttribArray(normalAttribute(), false);
			device.EnableVertexAttribArray(fixedPositionAttribute(), false);

			device.ActiveTexture(1);
			device.BindTexture(IGLDevice::Texture2D, 0);
			device.ActiveTexture(0);
			device.BindTexture(IGLDevice::Texture2D, 0);
		}

		void GLMapRenderer::RenderDynamicLightPass(std::vector<GLDynamicLight> lights) {
			SPADES_MARK_FUNCTION();

			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Map");

			if (lights.empty())
				return;

			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin;

			device.ActiveTexture(0);
			device.BindTexture(IGLDevice::Texture2D, 0);

			device.Enable(IGLDevice::CullFace, true);
			device.Enable(IGLDevice::DepthTest, true);

			dlightProgram->Use();

			static GLProgramUniform fogDistance("fogDistance");
			fogDistance(dlightProgram);
			fogDistance.SetValue(renderer.GetFogDistance());

			device.BindBuffer(IGLDevice::ArrayBuffer, 0);

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramAttribute colorAttribute("colorAttribute");
			static GLProgramAttribute normalAttribute("normalAttribute");

			positionAttribute(dlightProgram);
			colorAttribute(dlightProgram);
			normalAttribute(dlightProgram);

			device.EnableVertexAttribArray(positionAttribute(), true);
			device.EnableVertexAttribArray(colorAttribute(), true);
			device.EnableVertexAttribArray(normalAttribute(), true);

			static GLProgramUniform projectionViewMatrix("projectionViewMatrix");
			projectionViewMatrix(dlightProgram);
			projectionViewMatrix.SetValue(renderer.GetProjectionViewMatrix());

			static GLProgramUniform viewMatrix("viewMatrix");
			viewMatrix(dlightProgram);
			viewMatrix.SetValue(renderer.GetViewMatrix());

			static GLProgramUniform viewOriginVector("viewOriginVector");
			viewOriginVector(dlightProgram);
			viewOriginVector.SetValue(viewOrigin.x, viewOrigin.y, viewOrigin.z);

			// RealizeChunks(eye); // should already be realized from the prepass

			// draw from nearest to farthest
			IntVector3 c = viewOrigin.Floor() / GLMapChunk::Size;
			DrawColumnDLight(c.x, c.y, c.z, viewOrigin, lights);
			// TODO: optimize call
			//	ex. don't call a chunk'r render method if
			//  no dlight lights it
			for (int dist = 1; dist <= 128 / GLMapChunk::Size; dist++) {
				for (int x = c.x - dist; x <= c.x + dist; x++) {
					DrawColumnDLight(x, c.y + dist, c.z, viewOrigin, lights);
					DrawColumnDLight(x, c.y - dist, c.z, viewOrigin, lights);
				}
				for (int y = c.y - dist + 1; y <= c.y + dist - 1; y++) {
					DrawColumnDLight(c.x + dist, y, c.z, viewOrigin, lights);
					DrawColumnDLight(c.x - dist, y, c.z, viewOrigin, lights);
				}
			}

			device.EnableVertexAttribArray(positionAttribute(), false);
			device.EnableVertexAttribArray(colorAttribute(), false);
			device.EnableVertexAttribArray(normalAttribute(), false);

			device.ActiveTexture(0);
			device.BindTexture(IGLDevice::Texture2D, 0);
		}

		void GLMapRenderer::DrawColumnDepth(int cx, int cy, int cz, spades::Vector3 eye) {
			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			for (int z = std::max(cz, 0); z < numChunkDepth; z++)
				GetChunk(cx, cy, z)->RenderDepthPass();
			for (int z = std::min(cz - 1, 63); z >= 0; z--)
				GetChunk(cx, cy, z)->RenderDepthPass();
		}
		void GLMapRenderer::DrawColumnSunlight(int cx, int cy, int cz, spades::Vector3 eye) {
			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			for (int z = std::max(cz, 0); z < numChunkDepth; z++)
				GetChunk(cx, cy, z)->RenderSunlightPass();
			for (int z = std::min(cz - 1, 63); z >= 0; z--)
				GetChunk(cx, cy, z)->RenderSunlightPass();
		}

		void GLMapRenderer::DrawColumnDLight(int cx, int cy, int cz, spades::Vector3 eye,
		                                     const std::vector<GLDynamicLight>& lights) {
			cx &= numChunkWidth - 1;
			cy &= numChunkHeight - 1;
			for (int z = std::max(cz, 0); z < numChunkDepth; z++)
				GetChunk(cx, cy, z)->RenderDLightPass(lights);
			for (int z = std::min(cz - 1, 63); z >= 0; z--)
				GetChunk(cx, cy, z)->RenderDLightPass(lights);
		}

#pragma mark - BackFaceBlock

		struct BFVertex {
			int16_t x, y, z;
			uint16_t pad;

			static BFVertex Make(int x, int y, int z) {
				BFVertex v = {(int16_t)x, (int16_t)y, (int16_t)z, 0};
				return v;
			}
		};

		static void EmitBackFace(int x, int y, int z, int ux, int uy, int uz, int vx, int vy,
		                         int vz, std::vector<BFVertex>& vertices,
		                         std::vector<uint16_t>& indices) {
			uint16_t idx = (uint16_t)vertices.size();

			vertices.push_back(BFVertex::Make(x, y, z));
			vertices.push_back(BFVertex::Make(x + ux, y + uy, z + uz));
			vertices.push_back(BFVertex::Make(x + vx, y + vy, z + vz));
			vertices.push_back(BFVertex::Make(x + ux + vx, y + uy + vy, z + uz + vz));

			indices.push_back(idx);
			indices.push_back(idx + 1);
			indices.push_back(idx + 2);
			indices.push_back(idx + 1);
			indices.push_back(idx + 3);
			indices.push_back(idx + 2);
		}

		void GLMapRenderer::RenderBackface() {
			GLProfiler::Context profiler(renderer.GetGLProfiler(), "Back-face");
			const auto& viewOrigin = renderer.GetSceneDef().viewOrigin.Floor();

			std::vector<BFVertex> vertices;
			std::vector<uint16_t> indices;
			client::GameMap* m = gameMap;

			const int range = 1;
			for (int x = viewOrigin.x - range; x <= viewOrigin.x + range; x++) {
				for (int y = viewOrigin.y - range; y <= viewOrigin.y + range; y++) {
					for (int z = viewOrigin.z - range; z <= viewOrigin.z + range; z++) {
						if (z < 0 || z >= 63)
							continue;
						if (!m->IsSolidWrapped(x, y, z))
							continue;
						SPAssert(m->IsSolidWrapped(x, y, z));

						if (m->IsSolidWrapped(x - 1, y, z))
							EmitBackFace(x, y, z, 0, 1, 0, 0, 0, 1, vertices, indices);
						if (m->IsSolidWrapped(x + 1, y, z))
							EmitBackFace(x + 1, y, z, 0, 1, 0, 0, 0, 1, vertices, indices);
						if (m->IsSolidWrapped(x, y - 1, z))
							EmitBackFace(x, y, z, 1, 0, 0, 0, 0, 1, vertices, indices);
						if (m->IsSolidWrapped(x, y + 1, z))
							EmitBackFace(x, y + 1, z, 1, 0, 0, 0, 0, 1, vertices, indices);
						if (m->IsSolidWrapped(x, y, z - 1))
							EmitBackFace(x, y, z, 1, 0, 0, 0, 1, 0, vertices, indices);
						if (m->IsSolidWrapped(x, y, z + 1))
							EmitBackFace(x, y, z + 1, 1, 0, 0, 0, 1, 0, vertices, indices);
					}
				}
			}

			if (vertices.empty())
				return;

			device.Enable(IGLDevice::CullFace, false);

			backfaceProgram->Use();

			static GLProgramAttribute positionAttribute("positionAttribute");
			static GLProgramUniform projectionViewMatrix("projectionViewMatrix");

			positionAttribute(backfaceProgram);
			projectionViewMatrix(backfaceProgram);

			projectionViewMatrix.SetValue(renderer.GetProjectionViewMatrix());

			device.BindBuffer(IGLDevice::ArrayBuffer, 0);
			device.VertexAttribPointer(positionAttribute(), 3, IGLDevice::Short, false,
			                           sizeof(BFVertex), vertices.data());

			device.EnableVertexAttribArray(positionAttribute(), true);

			device.BindBuffer(IGLDevice::ElementArrayBuffer, 0);
			device.DrawElements(IGLDevice::Triangles, static_cast<IGLDevice::Sizei>(indices.size()),
			                    IGLDevice::UnsignedShort, indices.data());

			device.EnableVertexAttribArray(positionAttribute(), false);

			device.Enable(IGLDevice::CullFace, true);
		}
	} // namespace draw
} // namespace spades