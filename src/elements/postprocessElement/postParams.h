#ifndef _POST_PARAMS_H__
#define _POST_PARAMS_H__

#include <map>
#include <set>
#include <string>
#include <vector>

#define MAX_BBOX_PER_CLASS 512
#define MAX_BBOX_PER_IMG 1024

typedef enum {
    POSTPROCESS_CLUSTER_GROUP_RECTANGLES = 0,
    POSTPROCESS_CLUSTER_DBSCAN,
    POSTPROCESS_CLUSTER_NMS,
    POSTPROCESS_CLUSTER_NONE
} PostProcessClusterMode;

typedef enum {
    PostProcessNetworkType_Detector = 0,
    PostProcessNetworkType_Classifier,
    PostProcessNetworkType_Segmentation,
    PostProcessNetworkType_InstanceSegmentation,
    PostProcessNetworkType_Rtmpose,
    PostProcessNetworkType_Other = 100
} PostProcessNetworkType;

class PostProcessDetectionParams {
   public:
    PostProcessDetectionParams() {
        preClusterThreshold = 0.2;
        postClusterThreshold = 0;
        eps = 0;
        minBoxes = 0;
        groupThreshold = 0;
        minScore = 0;
        nmsIOUThreshold = 0.3;
        topK = -1;
        roiTopOffset = 0;
        roiBottomOffset = 0;
        detectionMinWidth = 0;
        detectionMinHeight = 0;
        detectionMaxWidth = 0;
        detectionMaxHeight = 0;
    };
    /** Holds the bounding box detection threshold to be applied prior
     * to clustering operation. */
    float preClusterThreshold;

    /** Hold the bounding box detection threshold to be applied post
     * clustering operation. */
    float postClusterThreshold;

    /** Holds the epsilon to control merging of overlapping boxes. Refer to OpenCV
     * groupRectangles and DBSCAN documentation for more information on epsilon. */
    float eps;
    /** Holds the minimum number of boxes in a cluster to be considered
     an object during grouping using DBSCAN. */
    int minBoxes;
    /** Holds the minimum number boxes in a cluster to be considered
     an object during grouping using OpenCV groupRectangles. */
    int groupThreshold;
    /** Minimum score in a cluster for the cluster to be considered an object
     during grouping. Different clustering may cause the algorithm
     to use different scores. */
    float minScore;
    /** ES_IOU threshold to be used with NMS mode of clustering. */
    float nmsIOUThreshold;
    /** Number of objects with objects to be filtered in the decensding order
     * of probability */
    int topK;

    unsigned int roiTopOffset;
    unsigned int roiBottomOffset;
    unsigned int detectionMinWidth;
    unsigned int detectionMinHeight;
    unsigned int detectionMaxWidth;
    unsigned int detectionMaxHeight;
};

enum EsPostOpttype { ESPOSTDSP = 0, ESPOSTCPU };

enum EsPostNetWorkName { ESYOLOV3 = 1, ESYOLOV4, ESYOLOV5, ESYOLOV7, ESYOLOV8, ESSSD, ESFRCNN };

enum ESNmsMethod { ES_HARD_NMS = 0, ES_SOFT_NMS_GAUSSIAN, ES_SOFT_NMS_LINEAR };

enum ES_IouMethod { ES_IOU = 0, ES_GIOU, ES_DIOU };

struct InputImgWH {
    InputImgWH() : W(416), H(416){};
    int W;
    int H;
};

struct OffSet {
    OffSet() : offX(0), offY(0){};
    int offX;
    int offY;
};

class DetectionOutParams {
   public:
    DetectionOutParams() {
        detectionName = ESYOLOV3;
        netName = "yolov3";
        nmsMethod = ES_HARD_NMS;
        iouMethod = ES_IOU;
        inputTensorNum = 3;
        outputTensorNum = 1;
        classNum = 80;
        anchorNum = 3;
        std::vector<int> scale0 = {116, 90, 156, 198, 373, 326};
        std::vector<int> scale1 = {30, 61, 62, 45, 59, 119};
        std::vector<int> scale2 = {10, 13, 16, 30, 33, 23};
        anchorScale.insert(anchorScale.end(), scale0.begin(), scale0.end());
        anchorScale.insert(anchorScale.end(), scale1.begin(), scale1.end());
        anchorScale.insert(anchorScale.end(), scale2.begin(), scale2.end());
        inputScale.clear();
        inputScale.push_back(0.000429799);
        inputScale.push_back(0.000376351);
        inputScale.push_back(0.0009178429);
        maxBboxPerClass = MAX_BBOX_PER_CLASS;
        maxBboxPerImg = MAX_BBOX_PER_IMG;
        scoreThreshold = 0.5;
        iouThreshold = 0.4;
        softnmssigma = 0.6;
    };

   public:
    EsPostNetWorkName detectionName;
    std::string netName;
    ESNmsMethod nmsMethod;
    ES_IouMethod iouMethod;
    InputImgWH inputWH;
    int inputTensorNum;
    int outputTensorNum;
    int classNum;
    int anchorNum;
    std::vector<float> anchorScale;
    std::vector<ES_FLOAT> inputScale;
    int maxBboxPerClass;
    int maxBboxPerImg;
    float scoreThreshold;
    float iouThreshold;
    float softnmssigma;
    OffSet imgOffset;
};
class RtmposeParams {
   public:
    RtmposeParams() {
        scale_x = 0.0001;
        scale_y = 0.0001;
        scoreThreshold = 0.1;
    };

   public:
    float scale_x;
    float scale_y;
    float scoreThreshold;
};

struct DetectionOutput {
    int batchID;
    int classID;
    float score;
    float left;
    float top;
    float w;
    float h;
};

class PostProcessInitParams {
   public:
    PostProcessInitParams() {
        m_targetInferID = 0;
        networkType = PostProcessNetworkType_Other;
        classifierThreshold = 0.5;
        segmentationThreshold = 0.5;
        clusterMode = POSTPROCESS_CLUSTER_NMS;
        dumpflag = false;
        dieID = 0;
        dspID = 0;
        optype = 0;
        softmaxScale = 1.0;
        modelHaveDspOp = false;
    };
    ~PostProcessInitParams(){};

   public:
    PostProcessNetworkType networkType;

    unsigned int m_targetInferID;

    std::string labelsFilePath;
    std::string modelOpMapFile;
    int optype;
    bool modelHaveDspOp;
    uint dieID;
    uint dspID;
    float softmaxScale;
    std::string goldDataPath;

    std::set<int> attachClassIds;
    PostProcessDetectionParams generalParams;
    std::map<int, PostProcessDetectionParams> specificParams;

    float classifierThreshold;
    std::string classifierType;

    float segmentationThreshold;
    int segmentationOutputOrder;

    PostProcessClusterMode clusterMode;
    bool dumpflag;

   public:
    DetectionOutParams detectionParams;
    RtmposeParams rtmposeParams;
};

/**
 * Holds information about one detected object.
 */
typedef struct {
    /** Holds the object's offset from the left boundary of the frame. */
    float left;
    /** Holds the object's offset from the top boundary of the frame. */
    float top;
    /** Holds the object's width. */
    float width;
    /** Holds the object's height. */
    float height;
    /** Holds the index for the object's class. */
    int classIndex;
    /** Holds a pointer to a string containing a label for the object. */
    char *label;
    /* confidence score of the detected object. */
    float confidence;
    /* Instance mask information for the object. */
    float *mask;
    /** Holds width of mask */
    unsigned int mask_width;
    /** Holds height of mask */
    unsigned int mask_height;
    /** Holds size of mask in bytes*/
    unsigned int mask_size;
} detectOneObjectInfo;

/**
 * Holds information about one classified attribute.
 */
typedef struct {
    /** Holds the index of the attribute's label. This index corresponds to
     the order of output layers specified in the @a outputCoverageLayerNames
     vector during initialization. */
    unsigned int attributeIndex;
    /** Holds the the attribute's output value. */
    unsigned int attributeValue;
    /** Holds the attribute's confidence level. */
    float attributeConfidence;
    /** Holds a pointer to a string containing the attribute's label.
     Memory for the string must not be freed. Custom parsing functions must
     allocate strings on heap using strdup or equivalent. */
    char *attributeLabel;
} classifyInfo;

/**
 * Holds information parsed from segmentation Info.
 */
typedef struct {
    /** Holds the width of the output. Same as network width. */
    unsigned int width;
    /** Holds the height of the output. Same as network height. */
    unsigned int height;
    /** Holds the number of classes supported by the network. */
    unsigned int classes;
    /** Holds a pointer to an array for the 2D pixel class map.
     The output for pixel (x,y) is at index (y*width+x). */
    int *class_map;
    /** Holds a pointer to an array containing raw probabilities.
     The probability for class @a c and pixel (x,y) is at index
     (c*width*height + y*width+x). */
    float *class_probability_map;
} SegmentationInfo;

typedef struct {
    std::vector<detectOneObjectInfo> objsInfoInOneFrame;
} DetectionOutputInOneFrame;

typedef struct {
    std::vector<classifyInfo> classifyInfoInOneFrame;
    int label_id;
    char *label;
} ClassifyOutputInOneFrame;

typedef struct {
    std::vector<SegmentationInfo> SegmentationInfoInOneFrame;
} SegmentateOutputInOneFrame;

typedef struct {
    PostProcessNetworkType outputType;

    DetectionOutputInOneFrame detectionOutput;
    ClassifyOutputInOneFrame classificationOutput;
    SegmentateOutputInOneFrame segmentationOutput;

} PostProcessOneFrameOutput;

typedef struct {
    std::vector<PostProcessOneFrameOutput> inferBatchOutput;
} PostProcessBatchOutput;

#endif  //_POST_PARAMS_H__