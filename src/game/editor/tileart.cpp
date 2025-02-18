#include "editor.h"

#include <game/editor/mapitems/image.h>

#include <array>

bool operator<(const ColorRGBA &Left, const ColorRGBA &Right)
{
	if(Left.r != Right.r)
		return Left.r < Right.r;
	else if(Left.g != Right.g)
		return Left.g < Right.g;
	else if(Left.b != Right.b)
		return Left.b < Right.b;
	else
		return Left.a < Right.a;
}

static ColorRGBA GetPixelColor(const CImageInfo &Image, size_t x, size_t y)
{
	uint8_t *pData = static_cast<uint8_t *>(Image.m_pData);
	const size_t PixelSize = Image.PixelSize();
	const size_t PixelStartIndex = x * PixelSize + (Image.m_Width * PixelSize * y);

	ColorRGBA Color = {255, 255, 255, 255};
	if(PixelSize == 1)
	{
		Color.a = pData[PixelStartIndex];
	}
	else
	{
		Color.r = pData[PixelStartIndex + 0];
		Color.g = pData[PixelStartIndex + 1];
		Color.b = pData[PixelStartIndex + 2];

		if(PixelSize == 4)
			Color.a = pData[PixelStartIndex + 3];
	}

	return Color;
}

static void SetPixelColor(CImageInfo *pImage, size_t x, size_t y, ColorRGBA Color)
{
	uint8_t *pData = static_cast<uint8_t *>(pImage->m_pData);
	const size_t PixelSize = pImage->PixelSize();
	const size_t PixelStartIndex = x * PixelSize + (pImage->m_Width * PixelSize * y);

	if(PixelSize == 1)
	{
		pData[PixelStartIndex] = Color.a;
	}
	else
	{
		pData[PixelStartIndex + 0] = Color.r;
		pData[PixelStartIndex + 1] = Color.g;
		pData[PixelStartIndex + 2] = Color.b;

		if(PixelSize == 4)
			pData[PixelStartIndex + 3] = Color.a;
	}
}

static std::vector<ColorRGBA> GetUniqueColors(const CImageInfo &Image)
{
	std::set<ColorRGBA> ColorSet;
	std::vector<ColorRGBA> vUniqueColors;
	for(int x = 0; x < Image.m_Width; x++)
	{
		for(int y = 0; y < Image.m_Height; y++)
		{
			ColorRGBA Color = GetPixelColor(Image, x, y);
			if(Color.a > 0 && ColorSet.insert(Color).second)
				vUniqueColors.push_back(Color);
		}
	}
	std::sort(vUniqueColors.begin(), vUniqueColors.end());

	return vUniqueColors;
}

constexpr int NumTilesRow = 16;
constexpr int NumTilesColumn = 16;
constexpr int NumTiles = NumTilesRow * NumTilesColumn;
constexpr int TileSize = 64;

static int GetColorIndex(const std::array<ColorRGBA, NumTiles> &ColorGroup, ColorRGBA Color)
{
	std::array<ColorRGBA, NumTiles>::const_iterator Iterator = std::find(ColorGroup.begin(), ColorGroup.end(), Color);
	if(Iterator == ColorGroup.end())
		return 0;
	return Iterator - ColorGroup.begin();
}

static std::vector<std::array<ColorRGBA, NumTiles>> GroupColors(const std::vector<ColorRGBA> &vColors)
{
	std::vector<std::array<ColorRGBA, NumTiles>> vaColorGroups;

	for(size_t i = 0; i < vColors.size(); i += NumTiles - 1)
	{
		auto &Group = vaColorGroups.emplace_back();
		std::copy_n(vColors.begin() + i, std::min<size_t>(NumTiles - 1, vColors.size() - i), Group.begin() + 1);
	}

	return vaColorGroups;
}

static void SetColorTile(CImageInfo *pImage, int x, int y, ColorRGBA Color)
{
	for(int i = 0; i < TileSize; i++)
	{
		for(int j = 0; j < TileSize; j++)
			SetPixelColor(pImage, x * TileSize + i, y * TileSize + j, Color);
	}
}

static CImageInfo ColorGroupToImage(const std::array<ColorRGBA, NumTiles> &aColorGroup)
{
	CImageInfo Image;
	Image.m_Width = NumTilesRow * TileSize;
	Image.m_Height = NumTilesColumn * TileSize;
	Image.m_Format = CImageInfo::FORMAT_RGBA;

	uint8_t *pData = static_cast<uint8_t *>(malloc(static_cast<size_t>(Image.m_Width) * Image.m_Height * 4 * sizeof(uint8_t)));
	Image.m_pData = pData;

	for(int y = 0; y < NumTilesColumn; y++)
	{
		for(int x = 0; x < NumTilesRow; x++)
		{
			int ColorIndex = x + NumTilesRow * y;
			SetColorTile(&Image, x, y, aColorGroup[ColorIndex]);
		}
	}

	return Image;
}

static std::vector<CImageInfo> ColorGroupsToImages(const std::vector<std::array<ColorRGBA, NumTiles>> &vaColorGroups)
{
	std::vector<CImageInfo> vImages;
	vImages.reserve(vaColorGroups.size());
	for(const auto &ColorGroup : vaColorGroups)
		vImages.push_back(ColorGroupToImage(ColorGroup));

	return vImages;
}

static std::shared_ptr<CEditorImage> ImageInfoToEditorImage(CEditor *pEditor, const CImageInfo &Image, const char *pName)
{
	std::shared_ptr<CEditorImage> pEditorImage = std::make_shared<CEditorImage>(pEditor);
	pEditorImage->m_Width = Image.m_Width;
	pEditorImage->m_Height = Image.m_Height;
	pEditorImage->m_Format = Image.m_Format;
	pEditorImage->m_pData = Image.m_pData;

	int TextureLoadFlag = pEditor->Graphics()->HasTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
	pEditorImage->m_Texture = pEditor->Graphics()->LoadTextureRaw(Image.m_Width, Image.m_Height, Image.m_Format, Image.m_pData, TextureLoadFlag, pName);
	pEditorImage->m_External = 0;
	str_copy(pEditorImage->m_aName, pName);

	return pEditorImage;
}

static std::shared_ptr<CLayerTiles> AddLayerWithImage(CEditor *pEditor, const std::shared_ptr<CLayerGroup> &pGroup, int Width, int Height, const CImageInfo &Image, const char *pName)
{
	std::shared_ptr<CEditorImage> pEditorImage = ImageInfoToEditorImage(pEditor, Image, pName);
	pEditor->m_Map.m_vpImages.push_back(pEditorImage);

	std::shared_ptr<CLayerTiles> pLayer = std::make_shared<CLayerTiles>(Width, Height);
	str_copy(pLayer->m_aName, pName);
	pLayer->m_pEditor = pEditor;
	pLayer->m_Image = pEditor->m_Map.m_vpImages.size() - 1;
	pGroup->AddLayer(pLayer);

	return pLayer;
}

static void SetTilelayerIndices(const std::shared_ptr<CLayerTiles> &pLayer, const std::array<ColorRGBA, NumTiles> &aColorGroup, const CImageInfo &Image)
{
	for(int x = 0; x < pLayer->m_Width; x++)
	{
		for(int y = 0; y < pLayer->m_Height; y++)
			pLayer->m_pTiles[x + y * pLayer->m_Width].m_Index = GetColorIndex(aColorGroup, GetPixelColor(Image, x, y));
	}
}

void CEditor::AddTileart()
{
	std::shared_ptr<CLayerGroup> pGroup = m_Map.NewGroup();
	str_copy(pGroup->m_aName, m_aTileartFilename);

	auto vUniqueColors = GetUniqueColors(m_TileartImageInfo);
	auto vaColorGroups = GroupColors(vUniqueColors);
	auto vColorImages = ColorGroupsToImages(vaColorGroups);
	char aImageName[IO_MAX_PATH_LENGTH];
	for(size_t i = 0; i < vColorImages.size(); i++)
	{
		str_format(aImageName, sizeof(aImageName), "%s %" PRIzu, m_aTileartFilename, i + 1);
		std::shared_ptr<CLayerTiles> pLayer = AddLayerWithImage(this, pGroup, m_TileartImageInfo.m_Width, m_TileartImageInfo.m_Height, vColorImages[i], aImageName);
		SetTilelayerIndices(pLayer, vaColorGroups[i], m_TileartImageInfo);
	}
	SortImages();

	free(m_TileartImageInfo.m_pData);
	m_TileartImageInfo.m_pData = nullptr;
	m_Map.OnModify();
	m_Dialog = DIALOG_NONE;
}

void CEditor::TileartCheckColors()
{
	auto vUniqueColors = GetUniqueColors(m_TileartImageInfo);
	int NumColorGroups = std::ceil(vUniqueColors.size() / 255.0f);
	if(m_Map.m_vpImages.size() + NumColorGroups >= 64)
	{
		m_PopupEventType = CEditor::POPEVENT_PIXELART_TOO_MANY_COLORS;
		m_PopupEventActivated = true;
		free(m_TileartImageInfo.m_pData);
		m_TileartImageInfo.m_pData = nullptr;
	}
	else if(NumColorGroups > 1)
	{
		m_PopupEventType = CEditor::POPEVENT_PIXELART_MANY_COLORS;
		m_PopupEventActivated = true;
	}
	else
		AddTileart();
}

bool CEditor::CallbackAddTileart(const char *pFilepath, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;

	if(!pEditor->Graphics()->LoadPNG(&pEditor->m_TileartImageInfo, pFilepath, StorageType))
	{
		pEditor->ShowFileDialogError("Failed to load image from file '%s'.", pFilepath);
		return false;
	}

	IStorage::StripPathAndExtension(pFilepath, pEditor->m_aTileartFilename, sizeof(pEditor->m_aTileartFilename));
	if(pEditor->m_TileartImageInfo.m_Width * pEditor->m_TileartImageInfo.m_Height > 10'000)
	{
		pEditor->m_PopupEventType = CEditor::POPEVENT_PIXELART_BIG_IMAGE;
		pEditor->m_PopupEventActivated = true;
		return false;
	}
	else
	{
		pEditor->TileartCheckColors();
		return false;
	}
}
