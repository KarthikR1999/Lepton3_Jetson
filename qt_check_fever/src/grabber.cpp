#include "grabber.h"
#include <QDebug>

Grabber::Grabber(QObject *parent) : QObject(parent)
{

}

Grabber::~Grabber()
{
    mStopRequest = true;

    while( !mGrabThread->isFinished() );
}

bool Grabber::openZed()
{
    if(mZed.isOpened())
    {
        mZed.close();
    }

    sl::InitParameters initParams;
    initParams.camera_fps = mZedFramerate;
    initParams.camera_resolution = mZedResol;

    sl::ERROR_CODE res = mZed.open( initParams );

    if( sl::ERROR_CODE::SUCCESS != res)
    {
        return false;
    }

    if( mZed.getCameraInformation().camera_model != sl::MODEL::ZED2)
    {
        return false;
    }

    mZedW = mZed.getCameraInformation().camera_configuration.resolution.width;
    mZedH = mZed.getCameraInformation().camera_configuration.resolution.height;

    return true;
}

void Grabber::startCapture()
{
    mGrabThread = QThread::create( &Grabber::grabFunc, this );

    mGrabThread->start();
}

sl::Mat& Grabber::getLastImage()
{
    return mZedRgb;
}

sl::Objects& Grabber::getLastObjDet()
{
    return mZedObj;
}

void Grabber::grabFunc()
{
    qDebug() << tr("Grabber thread started");

    mStopRequest = false;

    sl::ERROR_CODE err;

    sl::RuntimeParameters runtimeParams;
    runtimeParams.enable_depth = true;

    mObjRtParams.detection_confidence_threshold = 50.f;

    // ----> Positional tracking required by Object Detection module
    sl::PositionalTrackingParameters posTrackParams;
    posTrackParams.set_as_static=true;
    err = mZed.enablePositionalTracking(posTrackParams);
    if(err != sl::ERROR_CODE::SUCCESS)
    {
        qDebug() << tr("Error starting Positional Tracking: %1").arg(sl::toString(err).c_str());
        return;
    }
    // <---- Positional tracking required by Object Detection module

    // ----> Object Detection to detect Skeletons
    sl::ObjectDetectionParameters objDetPar;
    objDetPar.detection_model = sl::DETECTION_MODEL::HUMAN_BODY_FAST;
    objDetPar.enable_tracking = true;
    objDetPar.image_sync = false;
    objDetPar.enable_mask_output = false;
    err = mZed.enableObjectDetection( objDetPar );
    if(err != sl::ERROR_CODE::SUCCESS)
    {
        qDebug() << tr("Error starting Object Detectiom: %1").arg(sl::toString(err).c_str());
        return;
    }
    // <---- Object Detection to detect Skeletons

    forever
    {
        if( mStopRequest )
        {
            break;
        }

        err  = mZed.grab( runtimeParams );

        if(err != sl::ERROR_CODE::SUCCESS)
        {
            QThread::msleep( mZedFramerate/2 );
        }

        {
            QMutexLocker imgLocker(&mImgMutex);
            err = mZed.retrieveImage(mZedRgb, sl::VIEW::LEFT );
            if(err != sl::ERROR_CODE::SUCCESS)
            {
                qDebug() << tr("Error Retrieving Image: %1").arg(sl::toString(err).c_str());
                QThread::msleep( mZedFramerate/2 );
                continue;
            }
            emit zedImageReady();
        }

        err = mZed.retrieveMeasure(mZedDepth, sl::MEASURE::DEPTH);
        if(err != sl::ERROR_CODE::SUCCESS)
        {
            qDebug() << tr("Error Retrieving Depth: %1").arg(sl::toString(err).c_str());
            QThread::msleep( mZedFramerate/2 );
            continue;
        }

        {
            QMutexLocker objLocker( &mObjMutex );
            err = mZed.retrieveObjects(mZedObj, mObjRtParams );
            if(err != sl::ERROR_CODE::SUCCESS)
            {
                qDebug() << tr("Error Retrieving Objects: %1").arg(sl::toString(err).c_str());
                QThread::msleep( mZedFramerate/2 );
                continue;
            }

            if(mZedObj.is_new)
            {
                emit zedObjListReady();
                //qDebug() << tr("Found %1 persons").arg(mZedObj.object_list.size());
            }
        }
    }

    qDebug() << tr("Grabber thread finished");
}