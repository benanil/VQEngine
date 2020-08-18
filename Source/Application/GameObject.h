//	VQE
//	Copyright(C) 2020  - Volkan Ilbeyli
//
//	This program is free software : you can redistribute it and / or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation, either version 3 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program.If not, see <http://www.gnu.org/licenses/>.
//
//	Contact: volkanilbeyli@gmail.com

#pragma once

#include "Types.h"

#include <vector>

struct BoundingBox {};
class GameObject
{
public:
	TransformID mTransformID = INVALID_ID;
	ModelID     mModelID = INVALID_ID;

	// Todo: move to scene and rename to @BoundingBoxes_GameObjects;
	BoundingBox mBoundingBox;
	std::vector<BoundingBox> mMeshBoundingBoxes;
};
