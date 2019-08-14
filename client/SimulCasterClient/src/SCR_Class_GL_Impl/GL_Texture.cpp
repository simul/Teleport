// (C) Copyright 2018-2019 Simul Software Ltd
#include "GL_Texture.h"

using namespace scr;
using namespace OVR;

void GL_Texture::Create(Slot slot, Type type, Format format, SampleCount sampleCount, uint32_t width, uint32_t height, uint32_t depth, uint32_t bitsPerPixel, const uint8_t* data)
{
    m_Width = width;
    m_Height = height;
    m_Depth = depth;
    m_BitsPerPixel = bitsPerPixel;

    m_Slot = slot;
    m_Type = type;
    m_Format = format;
    m_SampleCount = sampleCount;

    m_Size = m_Width * m_Height * m_Depth * m_BitsPerPixel;
    m_Data = data;


    assert(TypeToGLTarget(m_Type) != 0);
    assert(ToGLFormat(m_Format) != 0);
    assert(ToBaseGLFormat(m_Format) != 0);

    m_Texture = GlTexture();

    glGenTextures(1, &m_Texture.texture);
    glBindTexture(TypeToGLTarget(m_Type), m_Texture.texture);

    switch (m_Type) {
        case Type::TEXTURE_UNKNOWN:
        {
            return;
        }
        case Type::TEXTURE_1D:
        {
            SCR_COUT("OpenGLES 3.0 doesn't support GL_TEXTURE_1D");
            return;
        }
        case Type::TEXTURE_2D:
        {
            glTexImage2D(TypeToGLTarget(m_Type), 0, ToGLFormat(m_Format), m_Width, m_Height, 0, ToBaseGLFormat(m_Format), GL_UNSIGNED_BYTE, m_Data);
            break;
        }
        case Type::TEXTURE_3D:
        {
            glTexImage3D(TypeToGLTarget(m_Type), 0, ToGLFormat(m_Format), m_Width, m_Height, m_Depth, 0, ToBaseGLFormat(m_Format), GL_UNSIGNED_BYTE, m_Data);
            break;
        }
        case Type::TEXTURE_1D_ARRAY:
        case Type::TEXTURE_2D_ARRAY:
        {
            glTexImage3D(TypeToGLTarget(m_Type), 0, ToGLFormat(m_Format), m_Width, m_Height, m_Depth, 0, ToBaseGLFormat(m_Format), GL_UNSIGNED_BYTE, m_Data);
            break;
        }
        case Type::TEXTURE_2D_MULTISAMPLE:
        {
            SCR_COUT("OpenGLES 3.0 doesn't support GL_TEXTURE_2D_MULTISAMPLE");
            return;
        }
        case Type::TEXTURE_2D_MULTISAMPLE_ARRAY:
        {
            SCR_COUT("OpenGLES 3.0 doesn't support GL_TEXTURE_2D_MULTISAMPLE_ARRAY");
            return;
        }
        case Type::TEXTURE_CUBE_MAP:
        {
            size_t offset = 0;
            for(uint32_t i = 0; i < 6; i++)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, ToGLFormat(m_Format), m_Width, m_Height, 0, ToBaseGLFormat(m_Format), GL_UNSIGNED_BYTE, m_Data + offset);
                offset += m_Size;
            }
            m_Size *= 6;
        }
        case Type::TEXTURE_CUBE_MAP_ARRAY:
        {
            SCR_COUT("OpenGLES 3.0 doesn't support GL_TEXTURE_CUBE_MAP_ARRAY");
            return;
        }

    }
}
void GL_Texture::Destroy()
{
    glDeleteTextures(1, &m_Texture.texture);
}

void GL_Texture::Bind() const
{
    glActiveTexture(GL_TEXTURE0 + static_cast<unsigned int>(m_Slot));
    glBindTexture(TypeToGLTarget(m_Type), m_Texture.texture);
}
void GL_Texture::Unbind() const
{
    glBindTexture((unsigned int)m_Type, 0);
}

void GL_Texture::UseSampler(const Sampler* sampler)
{
    m_Sampler = sampler;
    const GL_Sampler* glSampler = dynamic_cast<const GL_Sampler*>(m_Sampler);

    Bind();
    glTexParameteri(TypeToGLTarget(m_Type), GL_TEXTURE_WRAP_S, glSampler->ToGLWrapType(glSampler->GetWrapU()));
    glTexParameteri(TypeToGLTarget(m_Type), GL_TEXTURE_WRAP_T, glSampler->ToGLWrapType(glSampler->GetWrapV()));
    glTexParameteri(TypeToGLTarget(m_Type), GL_TEXTURE_WRAP_R, glSampler->ToGLWrapType(glSampler->GetWrapW()));

    glTexParameteri(TypeToGLTarget(m_Type), GL_TEXTURE_MIN_FILTER, glSampler->ToGLFilterType(glSampler->GetMinFilter()));
    glTexParameteri(TypeToGLTarget(m_Type), GL_TEXTURE_MAG_FILTER, glSampler->ToGLFilterType(glSampler->GetMagFilter()));

    if(glSampler->GetMinFilter() == Sampler::Filter::MIPMAP_LINEAR
        || glSampler->GetMinFilter() == Sampler::Filter::MIPMAP_NEAREST)
    {
        GenerateMips();
    }
    Unbind();
}
void GL_Texture::GenerateMips()
{
    glGenerateMipmap(TypeToGLTarget(m_Type));
}

GLenum GL_Texture::TypeToGLTarget(Type type) const
{
    switch (type)
    {
        case Type::TEXTURE_UNKNOWN: return 0;
        case Type::TEXTURE_1D: return 0; //GL_TEXTURE_1D;
        case Type::TEXTURE_2D: return GL_TEXTURE_2D;
        case Type::TEXTURE_3D: return GL_TEXTURE_3D;
        case Type::TEXTURE_1D_ARRAY: return 0; //GL_TEXTURE_2D_ARRAY;
        case Type::TEXTURE_2D_ARRAY: return GL_TEXTURE_2D_ARRAY;
        case Type::TEXTURE_2D_MULTISAMPLE: return GL_TEXTURE_2D_MULTISAMPLE;
        case Type::TEXTURE_2D_MULTISAMPLE_ARRAY: return GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
        case Type::TEXTURE_CUBE_MAP: return GL_TEXTURE_CUBE_MAP;
        case Type::TEXTURE_CUBE_MAP_ARRAY: return 0; //TEXTURE_CUBE_MAP_ARRAY;
    }
}

GLenum GL_Texture::ToBaseGLFormat(Format format) const
{
    switch(format) {
        case Format::FORMAT_UNKNOWN:
            return 0;

        case Format::RGBA32F:
        case Format::RGBA32UI:
        case Format::RGBA32I:
        case Format::RGBA16F:
        case Format::RGBA16UI:
        case Format::RGBA16I:
        case Format::RGBA16_SNORM:
        case Format::RGBA16:
        case Format::RGBA8UI:
        case Format::RGBA8I:
        case Format::RGBA8_SNORM:
        case Format::RGBA8:
            return GL_RGBA;

        case Format::R11F_G11F_B10F:
        case Format::RGB10_A2UI:
        case Format::RGB10_A2:
            return GL_RGB;

        case Format::RG32F:
        case Format::RG32UI:
        case Format::RG32I:
        case Format::RG16F:
        case Format::RG16UI:
        case Format::RG16I:
        case Format::RG16_SNORM:
        case Format::RG16:
        case Format::RG8UI:
        case Format::RG8I:
        case Format::RG8_SNORM:
        case Format::RG8:
            return GL_RG;

        case Format::R32F:
        case Format::R32UI:
        case Format::R32I:
        case Format::R16F:
        case Format::R16UI:
        case Format::R16I:
        case Format::R16_SNORM:
        case Format::R16:
        case Format::R8UI:
        case Format::R8I:
        case Format::R8_SNORM:
        case Format::R8:
            return GL_RED;

        case Format::DEPTH_COMPONENT32F:
        case Format::DEPTH_COMPONENT32:
        case Format::DEPTH_COMPONENT24:
        case Format::DEPTH_COMPONENT16:
            return GL_DEPTH_COMPONENT;

        case Format::DEPTH_STENCIL:
        case Format::DEPTH32F_STENCIL8:
        case Format::DEPTH24_STENCIL8:
            return GL_DEPTH_STENCIL;

        case Format::UNSIGNED_INT_24_8:
        case Format::FLOAT_32_UNSIGNED_INT_24_8_REV:
            return GL_RGBA;
    }
};

GLenum GL_Texture::ToGLFormat(Format format) const
{
    switch(format) {
        case Format::FORMAT_UNKNOWN: return 0;

        case Format::RGBA32F:           return GL_RGBA32F;
        case Format::RGBA32UI:          return GL_RGBA32UI;
        case Format::RGBA32I:           return GL_RGBA32I;
        case Format::RGBA16F:           return GL_RGBA16F;
        case Format::RGBA16UI:          return GL_RGBA16UI;
        case Format::RGBA16I:           return GL_RGBA16I;
        case Format::RGBA16_SNORM:      return 0; //GL_RGBA16_SNORM;
        case Format::RGBA16:            return 0; //GL_RGBA16;
        case Format::RGBA8UI:           return GL_RGBA8UI;
        case Format::RGBA8I:            return GL_RGBA8I;
        case Format::RGBA8_SNORM:       return GL_RGBA8_SNORM;
        case Format::RGBA8:             return GL_RGBA8;

        case Format::R11F_G11F_B10F:    return GL_R11F_G11F_B10F;
        case Format::RGB10_A2UI:        return GL_RGB10_A2UI;
        case Format::RGB10_A2:          return GL_RGB10_A2;

        case Format::RG32F:             return GL_RG32F;
        case Format::RG32UI:            return GL_RG32UI;
        case Format::RG32I:             return GL_RG32I;
        case Format::RG16F:             return GL_RG16F;
        case Format::RG16UI:            return GL_RG16UI;
        case Format::RG16I:             return GL_RG16I;
        case Format::RG16_SNORM:        return 0; //GL_RG16_SNORM;
        case Format::RG16:              return 0; //GL_RG16;
        case Format::RG8UI:             return GL_RG8UI;
        case Format::RG8I:              return GL_RG8I;
        case Format::RG8_SNORM:         return GL_RG8_SNORM;
        case Format::RG8:               return GL_RG8;

        case Format::R32F:              return GL_R32F;
        case Format::R32UI:             return GL_R32UI;
        case Format::R32I:              return GL_R32I;
        case Format::R16F:              return GL_R16F;
        case Format::R16UI:             return GL_R16UI;
        case Format::R16I:              return GL_R16I;
        case Format::R16_SNORM:         return 0; //GL_R16_SNORM;
        case Format::R16:               return 0; //GL_R16;
        case Format::R8UI:              return GL_R8UI;
        case Format::R8I:               return GL_R8I;
        case Format::R8_SNORM:          return GL_R8_SNORM;
        case Format::R8:                return GL_R8;

        case Format::DEPTH_COMPONENT32F:    return GL_DEPTH_COMPONENT32F;
        case Format::DEPTH_COMPONENT32:     return 0; //GL_DEPTH_COMPONENT32;
        case Format::DEPTH_COMPONENT24:     return GL_DEPTH_COMPONENT24;
        case Format::DEPTH_COMPONENT16:     return GL_DEPTH_COMPONENT16;

        case Format::DEPTH_STENCIL:         return GL_DEPTH_STENCIL;
        case Format::DEPTH32F_STENCIL8:     return GL_DEPTH32F_STENCIL8;
        case Format::DEPTH24_STENCIL8:      return GL_DEPTH24_STENCIL8;

        case Format::UNSIGNED_INT_24_8:                 return GL_UNSIGNED_INT_24_8;
        case Format::FLOAT_32_UNSIGNED_INT_24_8_REV:    return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;

    }
};