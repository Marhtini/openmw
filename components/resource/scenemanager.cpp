#include "scenemanager.hpp"

#include <osg/Node>
#include <osg/Geode>
#include <osg/UserDataContainer>
#include <osg/Version>

#include <osgParticle/ParticleSystem>

#include <osgUtil/IncrementalCompileOperation>

#include <osgViewer/Viewer>

#include <osgDB/SharedStateManager>
#include <osgDB/Registry>

#include <components/nifosg/nifloader.hpp>
#include <components/nif/niffile.hpp>

#include <components/vfs/manager.hpp>

#include <components/sceneutil/clone.hpp>
#include <components/sceneutil/util.hpp>
#include <components/sceneutil/controller.hpp>

#include "texturemanager.hpp"
#include "niffilemanager.hpp"

namespace
{

    class InitWorldSpaceParticlesVisitor : public osg::NodeVisitor
    {
    public:
        /// @param mask The node mask to set on ParticleSystem nodes.
        InitWorldSpaceParticlesVisitor(unsigned int mask)
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            , mMask(mask)
        {
        }

        bool isWorldSpaceParticleSystem(osgParticle::ParticleSystem* partsys)
        {
            // HACK: ParticleSystem has no getReferenceFrame()
            return (partsys->getUserDataContainer()
                    && partsys->getUserDataContainer()->getNumDescriptions() > 0
                    && partsys->getUserDataContainer()->getDescriptions()[0] == "worldspace");
        }

        void apply(osg::Geode& geode)
        {
            for (unsigned int i=0;i<geode.getNumDrawables();++i)
            {
                if (osgParticle::ParticleSystem* partsys = dynamic_cast<osgParticle::ParticleSystem*>(geode.getDrawable(i)))
                {
                    if (isWorldSpaceParticleSystem(partsys))
                    {
                        // HACK: Ignore the InverseWorldMatrix transform the geode is attached to
                        if (geode.getNumParents() && geode.getParent(0)->getNumParents())
                            transformInitialParticles(partsys, geode.getParent(0)->getParent(0));
                    }
                    geode.setNodeMask(mMask);
                }
            }
        }

#if OSG_VERSION_GREATER_OR_EQUAL(3,3,3)
        // in OSG 3.3 and up Drawables can be directly in the scene graph without a Geode decorating them.
        void apply(osg::Drawable& drw)
        {
            if (osgParticle::ParticleSystem* partsys = dynamic_cast<osgParticle::ParticleSystem*>(&drw))
            {
                if (isWorldSpaceParticleSystem(partsys))
                {
                    // HACK: Ignore the InverseWorldMatrix transform the particle system is attached to
                    if (partsys->getNumParents() && partsys->getParent(0)->getNumParents())
                        transformInitialParticles(partsys, partsys->getParent(0)->getParent(0));
                }
                partsys->setNodeMask(mMask);
            }
        }
#endif

        void transformInitialParticles(osgParticle::ParticleSystem* partsys, osg::Node* node)
        {
            osg::MatrixList mats = node->getWorldMatrices();
            if (mats.empty())
                return;
            osg::Matrixf worldMat = mats[0];
            worldMat.orthoNormalize(worldMat); // scale is already applied on the particle node
            for (int i=0; i<partsys->numParticles(); ++i)
            {
                partsys->getParticle(i)->transformPositionVelocity(worldMat);
            }

            // transform initial bounds to worldspace
            osg::BoundingSphere sphere(partsys->getInitialBound());
            SceneUtil::transformBoundingSphere(worldMat, sphere);
            osg::BoundingBox box;
            box.expandBy(sphere);
            partsys->setInitialBound(box);
        }
    private:
        unsigned int mMask;
    };
}

namespace Resource
{

    /// Set texture filtering settings on textures contained in a FlipController.
    class SetFilterSettingsControllerVisitor : public SceneUtil::ControllerVisitor
    {
    public:
        SetFilterSettingsControllerVisitor(osg::Texture::FilterMode minFilter, osg::Texture::FilterMode magFilter, int maxAnisotropy)
            : mMinFilter(minFilter)
            , mMagFilter(magFilter)
            , mMaxAnisotropy(maxAnisotropy)
        {
        }

        virtual void visit(osg::Node& node, SceneUtil::Controller& ctrl)
        {
            if (NifOsg::FlipController* flipctrl = dynamic_cast<NifOsg::FlipController*>(&ctrl))
            {
                for (std::vector<osg::ref_ptr<osg::Texture2D> >::iterator it = flipctrl->getTextures().begin(); it != flipctrl->getTextures().end(); ++it)
                {
                    osg::Texture* tex = *it;
                    tex->setFilter(osg::Texture::MIN_FILTER, mMinFilter);
                    tex->setFilter(osg::Texture::MAG_FILTER, mMagFilter);
                    tex->setMaxAnisotropy(mMaxAnisotropy);
                }
            }
        }

    private:
        osg::Texture::FilterMode mMinFilter;
        osg::Texture::FilterMode mMagFilter;
        int mMaxAnisotropy;
    };

    /// Set texture filtering settings on textures contained in StateSets.
    class SetFilterSettingsVisitor : public osg::NodeVisitor
    {
    public:
        SetFilterSettingsVisitor(osg::Texture::FilterMode minFilter, osg::Texture::FilterMode magFilter, int maxAnisotropy)
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN)
            , mMinFilter(minFilter)
            , mMagFilter(magFilter)
            , mMaxAnisotropy(maxAnisotropy)
        {
        }

        virtual void apply(osg::Node& node)
        {
            osg::StateSet* stateset = node.getStateSet();
            if (stateset)
                apply(stateset);
            traverse(node);
        }

        virtual void apply(osg::Geode& geode)
        {
            osg::StateSet* stateset = geode.getStateSet();
            if (stateset)
                apply(stateset);

            for (unsigned int i=0; i<geode.getNumDrawables(); ++i)
            {
                osg::Drawable* drw = geode.getDrawable(i);
                stateset = drw->getStateSet();
                if (stateset)
                    apply(stateset);
            }
        }

        void apply(osg::StateSet* stateset)
        {
            const osg::StateSet::TextureAttributeList& texAttributes = stateset->getTextureAttributeList();
            for(unsigned int unit=0;unit<texAttributes.size();++unit)
            {
                osg::StateAttribute *texture = stateset->getTextureAttribute(unit, osg::StateAttribute::TEXTURE);
                apply(texture);
            }
        }

        void apply(osg::StateAttribute* attr)
        {
            osg::Texture* tex = attr->asTexture();
            if (tex)
            {
                tex->setFilter(osg::Texture::MIN_FILTER, mMinFilter);
                tex->setFilter(osg::Texture::MAG_FILTER, mMagFilter);
                tex->setMaxAnisotropy(mMaxAnisotropy);
            }
        }
    private:
        osg::Texture::FilterMode mMinFilter;
        osg::Texture::FilterMode mMagFilter;
        int mMaxAnisotropy;
    };



    SceneManager::SceneManager(const VFS::Manager *vfs, Resource::TextureManager* textureManager, Resource::NifFileManager* nifFileManager)
        : mVFS(vfs)
        , mTextureManager(textureManager)
        , mNifFileManager(nifFileManager)
        , mMinFilter(osg::Texture::LINEAR_MIPMAP_LINEAR)
        , mMagFilter(osg::Texture::LINEAR)
        , mMaxAnisotropy(1)
        , mUnRefImageDataAfterApply(false)
        , mParticleSystemMask(~0u)
    {
    }

    SceneManager::~SceneManager()
    {
        // this has to be defined in the .cpp file as we can't delete incomplete types

    }

    /// @brief Callback to read image files from the VFS.
    class ImageReadCallback : public osgDB::ReadFileCallback
    {
    public:
        ImageReadCallback(Resource::TextureManager* textureMgr)
            : mTextureManager(textureMgr)
        {
        }

        virtual osgDB::ReaderWriter::ReadResult readImage(const std::string& filename, const osgDB::Options* options)
        {
            try
            {
                return osgDB::ReaderWriter::ReadResult(mTextureManager->getImage(filename), osgDB::ReaderWriter::ReadResult::FILE_LOADED);
            }
            catch (std::exception& e)
            {
                return osgDB::ReaderWriter::ReadResult(e.what());
            }
        }

    private:
        Resource::TextureManager* mTextureManager;
    };

    std::string getFileExtension(const std::string& file)
    {
        size_t extPos = file.find_last_of('.');
        if (extPos != std::string::npos && extPos+1 < file.size())
            return file.substr(extPos+1);
        return std::string();
    }

    osg::ref_ptr<osg::Node> load (Files::IStreamPtr file, const std::string& normalizedFilename, Resource::TextureManager* textureMgr, Resource::NifFileManager* nifFileManager)
    {
        std::string ext = getFileExtension(normalizedFilename);
        if (ext == "nif")
            return NifOsg::Loader::load(nifFileManager->get(normalizedFilename), textureMgr);
        else
        {
            osgDB::ReaderWriter* reader = osgDB::Registry::instance()->getReaderWriterForExtension(ext);
            if (!reader)
            {
                std::stringstream errormsg;
                errormsg << "Error loading " << normalizedFilename << ": no readerwriter for '" << ext << "' found" << std::endl;
                throw std::runtime_error(errormsg.str());
            }

            osg::ref_ptr<osgDB::Options> options (new osgDB::Options);
            // Set a ReadFileCallback so that image files referenced in the model are read from our virtual file system instead of the osgDB.
            // Note, for some formats (.obj/.mtl) that reference other (non-image) files a findFileCallback would be necessary.
            // but findFileCallback does not support virtual files, so we can't implement it.
            options->setReadFileCallback(new ImageReadCallback(textureMgr));

            osgDB::ReaderWriter::ReadResult result = reader->readNode(*file, options);
            if (!result.success())
            {
                std::stringstream errormsg;
                errormsg << "Error loading " << normalizedFilename << ": " << result.message() << " code " << result.status() << std::endl;
                throw std::runtime_error(errormsg.str());
            }
            return result.getNode();
        }
    }

    osg::ref_ptr<const osg::Node> SceneManager::getTemplate(const std::string &name)
    {
        std::string normalized = name;
        mVFS->normalizeFilename(normalized);

        Index::iterator it = mIndex.find(normalized);
        if (it == mIndex.end())
        {
            osg::ref_ptr<osg::Node> loaded;
            try
            {
                Files::IStreamPtr file = mVFS->get(normalized);

                loaded = load(file, normalized, mTextureManager, mNifFileManager);
            }
            catch (std::exception& e)
            {
                static const char * const sMeshTypes[] = { "nif", "osg", "osgt", "osgb", "osgx", "osg2" };

                for (unsigned int i=0; i<sizeof(sMeshTypes)/sizeof(sMeshTypes[0]); ++i)
                {
                    normalized = "meshes/marker_error." + std::string(sMeshTypes[i]);
                    if (mVFS->exists(normalized))
                    {
                        std::cerr << "Failed to load '" << name << "': " << e.what() << ", using marker_error." << sMeshTypes[i] << " instead" << std::endl;
                        Files::IStreamPtr file = mVFS->get(normalized);
                        loaded = load(file, normalized, mTextureManager, mNifFileManager);
                        break;
                    }
                }

                if (!loaded)
                    throw;
            }

            // set filtering settings
            SetFilterSettingsVisitor setFilterSettingsVisitor(mMinFilter, mMagFilter, mMaxAnisotropy);
            loaded->accept(setFilterSettingsVisitor);
            SetFilterSettingsControllerVisitor setFilterSettingsControllerVisitor(mMinFilter, mMagFilter, mMaxAnisotropy);
            loaded->accept(setFilterSettingsControllerVisitor);

            // share state
            osgDB::Registry::instance()->getOrCreateSharedStateManager()->share(loaded.get());

            if (mIncrementalCompileOperation)
                mIncrementalCompileOperation->add(loaded);

            mIndex[normalized] = loaded;
            return loaded;
        }
        else
            return it->second;
    }

    osg::ref_ptr<osg::Node> SceneManager::createInstance(const std::string &name)
    {
        osg::ref_ptr<const osg::Node> scene = getTemplate(name);
        osg::ref_ptr<osg::Node> cloned = osg::clone(scene.get(), SceneUtil::CopyOp());
        return cloned;
    }

    osg::ref_ptr<osg::Node> SceneManager::createInstance(const std::string &name, osg::Group* parentNode)
    {
        osg::ref_ptr<osg::Node> cloned = createInstance(name);
        attachTo(cloned, parentNode);
        return cloned;
    }

    void SceneManager::attachTo(osg::Node *instance, osg::Group *parentNode) const
    {
        parentNode->addChild(instance);
        notifyAttached(instance);
    }

    void SceneManager::releaseGLObjects(osg::State *state)
    {
        for (Index::iterator it = mIndex.begin(); it != mIndex.end(); ++it)
        {
            it->second->releaseGLObjects(state);
        }
    }

    void SceneManager::setIncrementalCompileOperation(osgUtil::IncrementalCompileOperation *ico)
    {
        mIncrementalCompileOperation = ico;
    }

    void SceneManager::notifyAttached(osg::Node *node) const
    {
        InitWorldSpaceParticlesVisitor visitor (mParticleSystemMask);
        node->accept(visitor);
    }

    const VFS::Manager* SceneManager::getVFS() const
    {
        return mVFS;
    }

    Resource::TextureManager* SceneManager::getTextureManager()
    {
        return mTextureManager;
    }

    void SceneManager::setParticleSystemMask(unsigned int mask)
    {
        mParticleSystemMask = mask;
    }

    void SceneManager::setFilterSettings(const std::string &magfilter, const std::string &minfilter,
                                           const std::string &mipmap, int maxAnisotropy,
                                           osgViewer::Viewer *viewer)
    {
        osg::Texture::FilterMode min = osg::Texture::LINEAR;
        osg::Texture::FilterMode mag = osg::Texture::LINEAR;

        if(magfilter == "nearest")
            mag = osg::Texture::NEAREST;
        else if(magfilter != "linear")
            std::cerr<< "Invalid texture mag filter: "<<magfilter <<std::endl;

        if(minfilter == "nearest")
            min = osg::Texture::NEAREST;
        else if(minfilter != "linear")
            std::cerr<< "Invalid texture min filter: "<<minfilter <<std::endl;

        if(mipmap == "nearest")
        {
            if(min == osg::Texture::NEAREST)
                min = osg::Texture::NEAREST_MIPMAP_NEAREST;
            else if(min == osg::Texture::LINEAR)
                min = osg::Texture::LINEAR_MIPMAP_NEAREST;
        }
        else if(mipmap != "none")
        {
            if(mipmap != "linear")
                std::cerr<< "Invalid texture mipmap: "<<mipmap <<std::endl;
            if(min == osg::Texture::NEAREST)
                min = osg::Texture::NEAREST_MIPMAP_LINEAR;
            else if(min == osg::Texture::LINEAR)
                min = osg::Texture::LINEAR_MIPMAP_LINEAR;
        }

        if(viewer) viewer->stopThreading();

        mMinFilter = min;
        mMagFilter = mag;
        mMaxAnisotropy = std::max(1, maxAnisotropy);

        SetFilterSettingsControllerVisitor setFilterSettingsControllerVisitor (mMinFilter, mMagFilter, mMaxAnisotropy);
        SetFilterSettingsVisitor setFilterSettingsVisitor (mMinFilter, mMagFilter, mMaxAnisotropy);
        if (viewer && viewer->getSceneData())
        {
            viewer->getSceneData()->accept(setFilterSettingsControllerVisitor);
            viewer->getSceneData()->accept(setFilterSettingsVisitor);
        }

        for (Index::iterator it = mIndex.begin(); it != mIndex.end(); ++it)
        {
            osg::Node* node = it->second;
            node->accept(setFilterSettingsControllerVisitor);
            node->accept(setFilterSettingsVisitor);
        }

        if(viewer) viewer->startThreading();
    }

    void SceneManager::applyFilterSettings(osg::Texture *tex)
    {
        tex->setFilter(osg::Texture::MIN_FILTER, mMinFilter);
        tex->setFilter(osg::Texture::MAG_FILTER, mMagFilter);
        tex->setMaxAnisotropy(mMaxAnisotropy);
    }

    void SceneManager::setUnRefImageDataAfterApply(bool unref)
    {
        mUnRefImageDataAfterApply = unref;
    }


}
