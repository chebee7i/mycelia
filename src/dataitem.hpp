/*
 * Mycelia immersive 3d network visualization tool.
 * Copyright (C) 2008-2010 Sean Whalen.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DATAITEM_HPP
#define __DATAITEM_HPP

#include <mycelia.hpp>

#include <map>
#include <vector>

class MyceliaDataItem : public GLObject::DataItem
{
public:
    /*GLMaterial* defaultNodeMaterial;
    GLMaterial* defaultEdgeMaterial;
    GLMaterial* previousMaterial;
    GLMaterial* selectedMaterial;*/
    GLUquadric* quadric;
    GLuint arrowList;
    GLuint graphList;
    GLuint nodeList;

    // cached images
    std::map<std::string, size_t> textureIndexMap;
    std::map<std::string, std::pair<int, int> > textureSizeMap;
    std::vector<GLuint> textureIds;

    typedef std::pair<int,int> SizePair;
    typedef std::pair< GLuint, SizePair > TexturePair;

    int graphListVersion;

    // fonts
    FTFont* font;

    MyceliaDataItem()
    {
        /*defaultNodeMaterial = new GLMaterial(GLMaterial::Color(1.0, 1.0, 1.0));
        defaultEdgeMaterial = new GLMaterial(GLMaterial::Color(0.3, 0.3, 0.3));
        previousMaterial = new GLMaterial(GLMaterial::Color(1.0, 0.5, 1.0));
        selectedMaterial = new GLMaterial(GLMaterial::Color(1.0, 0.0, 1.0));*/
        quadric = gluNewQuadric();
        arrowList = glGenLists(1);
        graphList = glGenLists(1);
        nodeList = glGenLists(1);

        textureIds.resize(1000); // reserve 1000 textures
        glGenTextures(1000, &textureIds[0]);

        graphListVersion = 0;
    }

    ~MyceliaDataItem()
    {
        /*delete defaultNodeMaterial;
        delete defaultEdgeMaterial;
        delete previousMaterial;
        delete selectedMaterial;*/
        gluDeleteQuadric(quadric);
        glDeleteLists(arrowList, 1);
        glDeleteLists(graphList, 1);
        glDeleteLists(nodeList, 1);
        glDeleteTextures(textureIds.size(), &textureIds[0]);
    }

    TexturePair getTextureId(std::string imagePath)
    {
        if (imagePath == "")
        {
            return TexturePair(0, SizePair(0,0) );
        }

        // Search for the path in the cache.
        std::map<std::string, size_t>::iterator it;
        it = textureIndexMap.find(imagePath);

        if ( it != textureIndexMap.end() )
        {
            // Exists.
            return TexturePair( textureIds[it->second], textureSizeMap[imagePath] );
        }

        // open a new image file
        //
        Images::RGBAImage image;
        try
        {
            image = Images::readTransparentImageFile(imagePath.c_str());
        }
        catch (...)
        {
            std::cerr << "Failed to load image: " << imagePath << std::endl;
            return TexturePair(0, SizePair(0,0));
        }
        SizePair size(image.getWidth(), image.getHeight());

        // grab a pregenerated texture id or generate a new one
        //
        unsigned int numCachedTextures = textureIndexMap.size();
        unsigned int imageIdIndex;
        if ( numCachedTextures < textureIds.size() )
        {
        }
        else
        {
            // make room for another texture id
            textureIds.push_back(0);
            glGenTextures(1, &(textureIds[0]) + numCachedTextures);
        }
        imageIdIndex = numCachedTextures;
        textureIndexMap[imagePath] = imageIdIndex;
        textureSizeMap[imagePath] = size;
        GLuint imageId = textureIds[imageIdIndex];

        // finally, load the image onto the graphics pipeline
        glBindTexture(GL_TEXTURE_2D, imageId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        image.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA);
        return TexturePair( imageId, size );
    }
};

#endif
