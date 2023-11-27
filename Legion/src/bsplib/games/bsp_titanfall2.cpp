#include "pch.h"
#include "bsplib.h"

#include "MdlLib.h"
#include "File.h"
#include "Directory.h"
#include "Path.h"

using namespace titanfall2;

void ExportTitanfall2Bsp(const std::unique_ptr<RpakLib>& RpakFileSystem, std::unique_ptr<IO::FileStream>& Stream, BSPHeader_t& header, const string& Asset, const string& Path)
{
	auto Model = std::make_unique<Assets::Model>(0, 0);

	auto ModelName = IO::Path::GetFileNameWithoutExtension(Asset);
	auto ModelPath = IO::Path::Combine(Path, ModelName);
	auto TexturePath = IO::Path::Combine(ModelPath, "_images");
	auto BasePath = IO::Path::Combine(IO::Path::GetDirectoryName(Asset), IO::Path::GetFileNameWithoutExtension(Asset));

	IO::Directory::CreateDirectory(TexturePath);

	Model->Name = ModelName;

	const uint32_t LUMP_TEXDATA = 0x2;
	const uint32_t LUMP_VERTEXES = 0x3;
	const uint32_t LUMP_MODELS = 0xE;
	const uint32_t LUMP_TEXDATA_STRING_DATA = 0x2B;
	const uint32_t LUMP_VERTNORMALS = 0x1E;
	const uint32_t LUMP_GAME_LUMPS = 0x23;
	const uint32_t LUMP_VERTEX_UNLIT = 0x47;
	const uint32_t LUMP_VERTEX_LIT_FLAT = 0x48;
	const uint32_t LUMP_VERTEX_LIT_BUMP = 0x49;
	const uint32_t LUMP_VERTEX_UNLIT_TS = 0x4A;
	const uint32_t LUMP_FACES = 0x4F;
	const uint32_t LUMP_MESHES = 0x50;
	const uint32_t LUMP_MATERIAL_SORT = 0x52;

	LumpLoadHelper_t helper{ header, &Stream, BasePath };

	List<string> NameStringTable;

	auto modelsLumpData = ReadLump<dmodel_t>(helper, LUMP_MODELS);

	auto meshesLumpData = ReadLump<dmesh_t>(helper, LUMP_MESHES);

	auto materialsLumpData = ReadLump<dmaterialsort_t>(helper, LUMP_MATERIAL_SORT);

	auto texLumpData = ReadLump<dtexdata_t>(helper, LUMP_TEXDATA);

	// material paths
	auto texStringLumpData = ReadLump<char>(helper, LUMP_TEXDATA_STRING_DATA);

	// face indices
	auto facesLumpData = ReadLump<uint16_t>(helper, LUMP_FACES);

	// vert points
	auto vertLumpData = ReadLump<Vector3>(helper, LUMP_VERTEXES);

	// vert normals
	auto vertNormalsLumpData = ReadLump<Vector3>(helper, LUMP_VERTNORMALS);

	// complex verts
	auto vertLitBumpLumpData = ReadLump<dvertLitBump>(helper, LUMP_VERTEX_LIT_BUMP);
	auto vertLitFlatLumpData = ReadLump<dvertLitFlat>(helper, LUMP_VERTEX_LIT_FLAT);
	auto vertUnlitLumpData = ReadLump<dvertUnlit>(helper, LUMP_VERTEX_UNLIT);
	auto vertUnlitTSLumpData = ReadLump<dvertUnlitTS>(helper, LUMP_VERTEX_UNLIT_TS);

	string temp;
	for (auto& e : texStringLumpData)
	{
		temp += e;
		if (e == '\0')
		{
			NameStringTable.EmplaceBack(temp);
			temp = "";
		}
	}

	std::unique_ptr<List<ApexAsset>> RpakMaterials;
	Dictionary<string, RpakLoadAsset> RpakMaterialLookup;

	// make sure that RpakFileSystem actually exists (i.e. an rpak is loaded)
	if (RpakFileSystem)
	{
		std::array<bool, 12> bAssets = {
			false, // Model
			false, // Animation
			false, // Animation Seq
			false, // Texture/Images
			true,  // Material
			false, // UIIA
			false, // Datatables
			false, // ShaderSets
			false, // SettingsSets
			false, // RSONs
			false, // Effects
			false, // Wrapped Files
		};

		RpakMaterials = RpakFileSystem->BuildAssetList(bAssets);

		for (auto& Mat : *RpakMaterials)
		{
			RpakMaterialLookup.Add(Mat.Name, RpakFileSystem->Assets[Mat.Hash]);
		}
	}

	for (auto& model : modelsLumpData)
	{
		for (uint32_t m = model.firstMesh; m < (model.firstMesh + model.meshCount); m++)
		{
			dmesh_t& BspMesh = meshesLumpData[m];

			if (BspMesh.triCount <= 0)
				continue;

			int FaceLump = BspMesh.flags & 0x600;

			dmaterialsort_t& Material = materialsLumpData[BspMesh.mtlSortIdx];
			Assets::Mesh& Mesh = Model->Meshes.Emplace(0, 1);

			dtexdata_t& tex = texLumpData[Material.texdata];
			string MaterialName = NameStringTable[tex.nameStringTableID];
			string CleanedMaterialName = IO::Path::GetFileNameWithoutExtension(MaterialName).ToLower();

			int32_t MaterialIndex = -1;

			if (RpakMaterialLookup.ContainsKey(CleanedMaterialName))
			{
				auto MaterialAsset = RpakMaterialLookup[CleanedMaterialName];
				auto ParsedMaterial = RpakFileSystem->ExtractMaterial(MaterialAsset, TexturePath, true, false);
				auto MaterialIndex = Model->AddMaterial(ParsedMaterial.MaterialName, ParsedMaterial.AlbedoHash);

				auto& MaterialInstance = Model->Materials[MaterialIndex];

				if (ParsedMaterial.AlbedoMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::Albedo, { "_images\\" + ParsedMaterial.AlbedoMapName, ParsedMaterial.AlbedoHash });
				if (ParsedMaterial.NormalMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::Normal, { "_images\\" + ParsedMaterial.NormalMapName, ParsedMaterial.NormalHash });
				if (ParsedMaterial.GlossMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::Gloss, { "_images\\" + ParsedMaterial.GlossMapName, ParsedMaterial.GlossHash });
				if (ParsedMaterial.SpecularMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::Specular, { "_images\\" + ParsedMaterial.SpecularMapName, ParsedMaterial.SpecularHash });
				if (ParsedMaterial.EmissiveMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::Emissive, { "_images\\" + ParsedMaterial.EmissiveMapName, ParsedMaterial.EmissiveHash });
				if (ParsedMaterial.AmbientOcclusionMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::AmbientOcclusion, { "_images\\" + ParsedMaterial.AmbientOcclusionMapName, ParsedMaterial.AmbientOcclusionHash });
				if (ParsedMaterial.CavityMapName != "")
					MaterialInstance.Slots.Add(Assets::MaterialSlotType::Cavity, { "_images\\" + ParsedMaterial.CavityMapName, ParsedMaterial.CavityHash });

				Mesh.MaterialIndices.EmplaceBack(MaterialIndex);
			}
			else
			{
				Mesh.MaterialIndices.EmplaceBack(Model->AddMaterial(CleanedMaterialName, 0xDEADBEEF));
			}

			uint32_t FaceIndex = 0;

			if (FaceLump == 0x000)
			{
				for (uint32_t v = BspMesh.firstIdx; v < (BspMesh.firstIdx + (BspMesh.triCount * 3)); v += 3)
				{
					dvertLitFlat& V1 = vertLitFlatLumpData[facesLumpData[v + 0] + Material.firstVertex];
					dvertLitFlat& V2 = vertLitFlatLumpData[facesLumpData[v + 1] + Material.firstVertex];
					dvertLitFlat& V3 = vertLitFlatLumpData[facesLumpData[v + 2] + Material.firstVertex];

					Mesh.Vertices.EmplaceBack(vertLumpData[V1.posIdx], vertNormalsLumpData[V1.nmlIdx], Assets::VertexColor(), V1.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V2.posIdx], vertNormalsLumpData[V2.nmlIdx], Assets::VertexColor(), V2.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V3.posIdx], vertNormalsLumpData[V3.nmlIdx], Assets::VertexColor(), V3.tex);

					Mesh.Faces.EmplaceBack(FaceIndex, FaceIndex + 1, FaceIndex + 2);
					FaceIndex += 3;
				}
			}
			else if (FaceLump == 0x200)
			{
				for (uint32_t v = BspMesh.firstIdx; v < (BspMesh.firstIdx + (BspMesh.triCount * 3)); v += 3)
				{
					dvertLitBump& V1 = vertLitBumpLumpData[facesLumpData[v + 0] + Material.firstVertex];
					dvertLitBump& V2 = vertLitBumpLumpData[facesLumpData[v + 1] + Material.firstVertex];
					dvertLitBump& V3 = vertLitBumpLumpData[facesLumpData[v + 2] + Material.firstVertex];

					Mesh.Vertices.EmplaceBack(vertLumpData[V1.posIdx], vertNormalsLumpData[V1.nmlIdx], Assets::VertexColor(), V1.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V2.posIdx], vertNormalsLumpData[V2.nmlIdx], Assets::VertexColor(), V2.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V3.posIdx], vertNormalsLumpData[V3.nmlIdx], Assets::VertexColor(), V3.tex);

					Mesh.Faces.EmplaceBack(FaceIndex, FaceIndex + 1, FaceIndex + 2);
					FaceIndex += 3;
				}
			}
			else if (FaceLump == 0x400)
			{
				for (uint32_t v = BspMesh.firstIdx; v < (BspMesh.firstIdx + (BspMesh.triCount * 3)); v += 3)
				{
					dvertUnlit& V1 = vertUnlitLumpData[facesLumpData[v + 0] + Material.firstVertex];
					dvertUnlit& V2 = vertUnlitLumpData[facesLumpData[v + 1] + Material.firstVertex];
					dvertUnlit& V3 = vertUnlitLumpData[facesLumpData[v + 2] + Material.firstVertex];

					Mesh.Vertices.EmplaceBack(vertLumpData[V1.posIdx], vertNormalsLumpData[V1.nmlIdx], Assets::VertexColor(), V1.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V2.posIdx], vertNormalsLumpData[V2.nmlIdx], Assets::VertexColor(), V2.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V3.posIdx], vertNormalsLumpData[V3.nmlIdx], Assets::VertexColor(), V3.tex);

					Mesh.Faces.EmplaceBack(FaceIndex, FaceIndex + 1, FaceIndex + 2);
					FaceIndex += 3;
				}
			}
			else if (FaceLump == 0x600)
			{
				for (uint32_t v = BspMesh.firstIdx; v < (BspMesh.firstIdx + (BspMesh.triCount * 3)); v += 3)
				{
					dvertUnlitTS& V1 = vertUnlitTSLumpData[facesLumpData[v + 0] + Material.firstVertex];
					dvertUnlitTS& V2 = vertUnlitTSLumpData[facesLumpData[v + 1] + Material.firstVertex];
					dvertUnlitTS& V3 = vertUnlitTSLumpData[facesLumpData[v + 2] + Material.firstVertex];

					Mesh.Vertices.EmplaceBack(vertLumpData[V1.posIdx], vertNormalsLumpData[V1.nmlIdx], Assets::VertexColor(), V1.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V2.posIdx], vertNormalsLumpData[V2.nmlIdx], Assets::VertexColor(), V2.tex);
					Mesh.Vertices.EmplaceBack(vertLumpData[V3.posIdx], vertNormalsLumpData[V3.nmlIdx], Assets::VertexColor(), V3.tex);

					Mesh.Faces.EmplaceBack(FaceIndex, FaceIndex + 1, FaceIndex + 2);
					FaceIndex += 3;
				}
			}
		}
	}

	s_BSPModelExporter->ExportModel(*Model.get(), IO::Path::Combine(ModelPath, Model->Name + "_LOD0" + (const char*)s_BSPModelExporter->ModelExtension()));

	uint8_t* Buffer = new uint8_t[header.lumps[LUMP_GAME_LUMPS].filelen];
	if (header.bExternal)
	{
		ReadExternalLumpArrayFile(BasePath, LUMP_GAME_LUMPS, header.lumps[LUMP_GAME_LUMPS].filelen, &Buffer[0]);
	}
	else {
		Stream->SetPosition(header.lumps[LUMP_GAME_LUMPS].fileofs);
		Stream->Read(Buffer, 0, header.lumps[LUMP_GAME_LUMPS].filelen);
	}

	std::unique_ptr<IO::MemoryStream> Stream2 = std::make_unique<IO::MemoryStream>(Buffer, 0, header.lumps[LUMP_GAME_LUMPS].filelen);
	List<string> propNames = ExportPropContainer(Stream2, Model->Name + "_LOD0", ModelPath);

	// Export all of the bsp's prop models

	if (!RpakFileSystem)
		return;

	string ExportedModelsPath = IO::Path::Combine(ModelPath, "_models");
	string ExportedModelAnimsPath = IO::Path::Combine(ExportedModelsPath, "_animations");

	std::array<bool, 12> bAssets = {
		true,  // Model
		false, // Animation
		false, // AnimationSeq
		false, // Texture/Images
		false, // Material
		false, // UIIA
		false, // Datatables
		false, // ShaderSets
		false, // SettingsSets
		false, // RSONs
		false, // Effects
		false, // Wrapped Files
	};

	auto RpakModels = RpakFileSystem->BuildAssetList(bAssets);
	Dictionary<string, RpakLoadAsset> RpakModelLookup;

	for (auto& tm : *RpakModels)
	{
		RpakModelLookup.Add(tm.Name, RpakFileSystem->Assets[tm.Hash]);
	}

	for (uint32_t i = 0; i < propNames.Count(); ++i)
	{
		string ModelName = propNames[i];
		if (RpakModelLookup.ContainsKey(ModelName))
		{
			RpakLoadAsset& ModelAsset = RpakModelLookup[ModelName];
			RpakFileSystem->ExportModel(ModelAsset, ExportedModelsPath, ExportedModelAnimsPath);
		}
	}
}

static game_t game_titanfall2{
	"Titanfall 2",
	36,
	37,
	'PSBr',
	ExportTitanfall2Bsp
};

REGISTER_BSP(game_titanfall2);
