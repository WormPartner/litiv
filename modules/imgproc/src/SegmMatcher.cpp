
// This file is part of the LITIV framework; visit the original repository at
// https://github.com/plstcharles/litiv for more information.
//
// Copyright 2016 Pierre-Luc St-Charles; pierre-luc.st-charles<at>polymtl.ca
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "litiv/imgproc/SegmMatcher.hpp"
#include "litiv/3rdparty/ofdis/ofdis.hpp"

// config options
#define SEGMMATCH_CONFIG_USE_DASCGF_AFFINITY   0
#define SEGMMATCH_CONFIG_USE_DASCRF_AFFINITY   1
#define SEGMMATCH_CONFIG_USE_LSS_AFFINITY      0
#define SEGMMATCH_CONFIG_USE_MI_AFFINITY       0
#define SEGMMATCH_CONFIG_USE_SSQDIFF_AFFINITY  0
#define SEGMMATCH_CONFIG_USE_SHAPE_EMD_AFFIN   0
#define SEGMMATCH_CONFIG_USE_SALIENT_MAP_BORDR 1
#define SEGMMATCH_CONFIG_USE_ROOT_SIFT_DESCS   0
#define SEGMMATCH_CONFIG_USE_DISP_BG_HRST      0
#define SEGMMATCH_CONFIG_USE_GMM_LOCAL_BACKGR  1
#define SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF   1
#define SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF 0
#define SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF  0
#define SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF   0
#define SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF  1
#define SEGMMATCH_CONFIG_USE_PROGRESS_BARS     0
#define SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN     0
#define SEGMMATCH_CONFIG_USE_TEMPORAL_CONN     1
#define SEGMMATCH_CONFIG_USE_MEDIAN_DIST_COST  1
#define SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS   0
#define SEGMMATCH_CONFIG_USE_FULL_DISP_RESETS  0
#define SEGMMATCH_CONFIG_USE_CONT_RESEGM_UPDT  1
#define SEGMMATCH_CONFIG_USE_TEMPORAL_U_CST    0
#define SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST 1
#define SEGMMATCH_CONFIG_USE_LAST_STEREO_INIT  1

// default param values
#define SEGMMATCH_DEFAULT_TEMPORAL_DEPTH       (size_t(1))
#define SEGMMATCH_DEFAULT_DISPARITY_STEP       (size_t(1))
#define SEGMMATCH_DEFAULT_MAX_STEREO_ITER      (size_t(500))
#define SEGMMATCH_DEFAULT_MAX_RESEGM_ITER      (size_t(30))
#define SEGMMATCH_DEFAULT_SCDESC_WIN_RAD       (size_t(50))
#define SEGMMATCH_DEFAULT_SCDESC_RAD_BINS      (size_t(3))
#define SEGMMATCH_DEFAULT_SCDESC_ANG_BINS      (size_t(10))
#define SEGMMATCH_DEFAULT_LSSDESC_RAD          (size_t(40))
#define SEGMMATCH_DEFAULT_LSSDESC_PATCH        (size_t(7))
#define SEGMMATCH_DEFAULT_LSSDESC_RAD_BINS     (size_t(3))
#define SEGMMATCH_DEFAULT_LSSDESC_ANG_BINS     (size_t(10))
#define SEGMMATCH_DEFAULT_SSQDIFF_PATCH        (size_t(7))
#define SEGMMATCH_DEFAULT_MI_WINDOW_RAD        (size_t(12))
#define SEGMMATCH_DEFAULT_GRAD_KERNEL_SIZE     (int(1))
#define SEGMMATCH_DEFAULT_ITER_PER_RESEGM      (m_nStereoLabels)
#define SEGMMATCH_DEFAULT_SALIENT_SHP_RAD      (3)
#define SEGMMATCH_DEFAULT_DESC_PATCH_SIZE      (15)
#define SEGMMATCH_DEFAULT_CONTOUR_DC_SIZE      (0)
#define SEGMMATCH_DEFAULT_BG_ZONE_SIZE         (45)
#define SEGMMATCH_DEFAULT_GMM_3CH_COMPONENTS   (6)
#define SEGMMATCH_DEFAULT_GMM_1CH_COMPONENTS   (3)

// unary costs params
#define SEGMMATCH_UNARY_COST_TEMPORAL_CST      (ValueType(200))
#define SEGMMATCH_IMGSIM_COST_COLOR_SCALE      (30)
#define SEGMMATCH_IMGSIM_COST_DESC_SCALE       (1000)
#define SEGMMATCH_SHPSIM_COST_DESC_SCALE       (1000)
#define SEGMMATCH_UNIQUE_COST_OVER_SCALE       (400)
#define SEGMMATCH_SHPDIST_COST_SCALE           (200)
#define SEGMMATCH_SHPDIST_PX_MAX_CST           (10.0f)
#define SEGMMATCH_SHPDIST_INTERSPEC_SCALE      (0.50f)
// pairwise costs params
#define SEGMMATCH_LBLSIM_RESEGM_SCALE_CST      (200)
#define SEGMMATCH_LBLSIM_STEREO_SCALE_CST      (1.f)
#define SEGMMATCH_LBLSIM_STEREO_MAXDIFF_CST    (std::max(10,(int)m_nMaxDispOffset/8))
#define SEGMMATCH_LBLSIM_MEDIAN_DIST_SCALE_CST (20)
#define SEGMMATCH_LBLSIM_COST_GRADRAW_SCALE    (30)
#define SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST    (30)
// higher order costs params
#define SEGMMATCH_HOENERGY_STEREO_STRIDE       (size_t(1))
#define SEGMMATCH_HOENERGY_RESEGM_STRIDE       (size_t(2))
// hardcoded term relations
#define SEGMMATCH_UNIQUE_COST_INCR_REL(n)      (float((n)*3)/((n)+2))
#define SEGMMATCH_UNIQUE_COST_ZERO_COUNT       (1)

#if (SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF || SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF)
#if !HAVE_OPENGM_EXTLIB
#error "SegmMatcher config requires OpenGM external lib w/ QPBO for inference."
#endif //!HAVE_OPENGM_EXTLIB
#if !HAVE_OPENGM_EXTLIB_QPBO
#error "SegmMatcher config requires OpenGM external lib w/ QPBO for inference."
#endif //!HAVE_OPENGM_EXTLIB_QPBO
#endif //(SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF || SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF)
#if SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
#if !HAVE_OPENGM_EXTLIB
#error "SegmMatcher config requires OpenGM external lib w/ FastPD for inference."
#endif //!HAVE_OPENGM_EXTLIB
#if !HAVE_OPENGM_EXTLIB_FASTPD
#error "SegmMatcher config requires OpenGM external lib w/ FastPD for inference."
#endif //!HAVE_OPENGM_EXTLIB_FASTPD
#endif //SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
#if (SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF)
#if !HAVE_BOOST
#error "SegmMatcher config requires boost due to 3rdparty sospd module for inference."
#endif //!HAVE_BOOST
#define SEGMMATCH_CONFIG_USE_SOSPD_ALPHA_HEIGHTS_LABEL_ORDERING 0
#endif //(SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF)
#if (SEGMMATCH_CONFIG_USE_DASCGF_AFFINITY+\
     SEGMMATCH_CONFIG_USE_DASCRF_AFFINITY+\
     SEGMMATCH_CONFIG_USE_LSS_AFFINITY+\
     SEGMMATCH_CONFIG_USE_MI_AFFINITY+\
     SEGMMATCH_CONFIG_USE_SSQDIFF_AFFINITY/*+...*/\
    )!=1
#error "Must specify only one image affinity map computation approach to use."
#endif //(features config ...)!=1
#define SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY (SEGMMATCH_CONFIG_USE_DASCGF_AFFINITY||SEGMMATCH_CONFIG_USE_DASCRF_AFFINITY||SEGMMATCH_CONFIG_USE_LSS_AFFINITY)
#if (SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF+\
     SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF+\
     SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF/*+...*/\
    )!=1
#error "Must specify only one stereo inference approach to use."
#endif //(stereo inf config ...)!=1
#if (SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF+\
     SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF/*+...*/\
    )!=1
#error "Must specify only one resegm inference approach to use."
#endif //(resegm inf config ...)!=1

namespace {

    using InternalLabelType = SegmMatcher::InternalLabelType;
    using OutputLabelType = SegmMatcher::OutputLabelType;
    using AssocCountType = SegmMatcher::AssocCountType;
    using AssocIdxType = SegmMatcher::AssocIdxType;
    using ValueType =  SegmMatcher::ValueType;
    using IndexType = SegmMatcher::IndexType;

    using ExplicitFunction = lv::gm::ExplicitViewFunction<ValueType,IndexType,InternalLabelType>; ///< shortcut for explicit view function
    using ExplicitAllocFunction = opengm::ExplicitFunction<ValueType,IndexType,InternalLabelType>; ///< shortcut for explicit allocated function
    using ExplicitScaledFunction = lv::gm::ExplicitScaledViewFunction<ValueType,IndexType,InternalLabelType>; ///< shortcut for explicit scaled view function
    using FunctionTypeList = opengm::meta::TypeListGenerator<ExplicitFunction,ExplicitAllocFunction,ExplicitScaledFunction>::type;  ///< list of all functions the models can use
    constexpr size_t s_nEpipolarCliqueOrder = SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN?size_t(3):size_t(0); ///< epipolar clique order (i.e. node count)
    constexpr size_t s_nEpipolarCliqueEdges = SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN?size_t(2):size_t(0); ///< epipolar clique edge count (i.e. connections to main node)
    constexpr size_t s_nEpipolarCliqueStride = SEGMMATCH_HOENERGY_STEREO_STRIDE; ///< epipolar clique stride size (i.e. skipped connections; 1=fully connected)
    static_assert(s_nEpipolarCliqueStride>size_t(0),"stereo clique stride must be strictly positive");
    constexpr size_t s_nTemporalCliqueDepth = SEGMMATCH_CONFIG_USE_TEMPORAL_CONN?SEGMMATCH_DEFAULT_TEMPORAL_DEPTH:size_t(0); ///< temporal depth level (i.e. connectivity layers)
    constexpr size_t s_nTemporalCliqueOrder = SEGMMATCH_CONFIG_USE_TEMPORAL_CONN?(s_nTemporalCliqueDepth+1):size_t(0); ///< temporal clique order (i.e. node count)
    constexpr size_t s_nTemporalCliqueEdges = SEGMMATCH_CONFIG_USE_TEMPORAL_CONN?s_nTemporalCliqueDepth:size_t(0); ///< temporal clique edge count (i.e. connections to main node)
    constexpr size_t s_nTemporalCliqueStride = SEGMMATCH_HOENERGY_RESEGM_STRIDE; ///< temporal clique stride size (i.e. skipped connections; 1=fully connected)
    static_assert(s_nTemporalCliqueStride>size_t(0),"resegm clique stride must be strictly positive");
    static constexpr size_t getTemporalLayerCount() {return s_nTemporalCliqueDepth+1;} ///< returns the expected temporal layer count
    using PairwClique = lv::gm::Clique<size_t(2),ValueType,IndexType,InternalLabelType>; ///< pairwise clique implementation wrapper
    using EpipolarClique = lv::gm::Clique<s_nEpipolarCliqueOrder,ValueType,IndexType,InternalLabelType>; ///< stereo epipolar line clique implementation wrapper
    using TemporalClique = lv::gm::Clique<s_nTemporalCliqueOrder,ValueType,IndexType,InternalLabelType>; ///< resegm temporal clique implementation wrapper
    using Clique = lv::gm::IClique<ValueType,IndexType,InternalLabelType>; ///< general-use clique implementation wrapper for all terms
    using StereoSpaceType = opengm::SimpleDiscreteSpace<IndexType,InternalLabelType>; ///< shortcut for discrete stereo space type (simple = all nodes have the same # of labels)
    using ResegmSpaceType = opengm::StaticSimpleDiscreteSpace<2,IndexType,InternalLabelType>; ///< shortcut for discrete resegm space type (binary labels for fg/bg)
    using StereoModelType = opengm::GraphicalModel<ValueType,opengm::Adder,FunctionTypeList,StereoSpaceType>; ///< shortcut for stereo graphical model type
    using ResegmModelType = opengm::GraphicalModel<ValueType,opengm::Adder,FunctionTypeList,ResegmSpaceType>; ///< shortcut for resegm graphical model type
    static_assert(std::is_same<StereoModelType::FunctionIdentifier,ResegmModelType::FunctionIdentifier>::value,"mismatched function identifier for stereo/resegm graphs");
    using FuncIdentifType = StereoModelType::FunctionIdentifier; ///< shortcut for graph model function identifier type (for both stereo and resegm models)
    using FuncPairType = std::pair<FuncIdentifType,ExplicitFunction&>; ///< funcid-funcobj pair used as viewer to explicit data (for both stereo and resegm models)
    using ScaledFuncPairType = std::pair<FuncIdentifType,ExplicitScaledFunction&>; ///< funcid-funcobj pair used as scaled viewer to explicit data (for stereo pairw factors)
    constexpr size_t s_nMaxOrder = lv::get_next_pow2((uint32_t)std::max(std::max(s_nTemporalCliqueOrder,s_nEpipolarCliqueOrder),size_t(2))); ///< used to limit internal static array sizes
    constexpr size_t s_nPairwOrients = size_t(2); ///< number of pairwise links owned by each node in the graph (2 = 1st order neighb connections)
    static_assert(s_nPairwOrients>size_t(0),"pairwise orientation count must be strictly positive");
    template<typename T> using CamArray = SegmMatcher::CamArray<T>; ///< shortcut typename for variables and members that are assigned to each camera head
    template<typename T> using TemporalArray = std::array<T,getTemporalLayerCount()>; ///< shortcut typename for variables and members that are assigned to each temporal layer

    /// defines the indices of feature maps inside precalc packets (per camera head)
    enum FeatPackingList {
        FeatPackSize=18,
        FeatPackOffset=7,
        // absolute values for direct indexing
        FeatPack_LeftFGDist=0,
        FeatPack_LeftBGDist=1,
        FeatPack_LeftGradY=2,
        FeatPack_LeftGradX=3,
        FeatPack_LeftGradMag=4,
        FeatPack_LeftOptFlow=5,
        FeatPack_LeftTempDiff=6,
        FeatPack_RightFGDist=7,
        FeatPack_RightBGDist=8,
        FeatPack_RightGradY=9,
        FeatPack_RightGradX=10,
        FeatPack_RightGradMag=11,
        FeatPack_RightOptFlow=12,
        FeatPack_RightTempDiff=13,
        FeatPack_ImgSaliency=14,
        FeatPack_ShpSaliency=15,
        FeatPack_ImgAffinity=16,
        FeatPack_ShpAffinity=17,
        // relative values for cam-based indexing
        FeatPackOffset_FGDist=0,
        FeatPackOffset_BGDist=1,
        FeatPackOffset_GradY=2,
        FeatPackOffset_GradX=3,
        FeatPackOffset_GradMag=4,
        FeatPackOffset_OptFlow=5,
        FeatPackOffset_TempDiff=6,

    };

    /// basic info struct used for node-level graph model updates and data lookups
    struct NodeInfo {
        /// image grid coordinates associated with this node
        int nRowIdx,nColIdx;
        /// holds whether this is a valid graph node or not
        bool bValidGraphNode;
        /// if valid, this holds whether this node is near graph borders or not
        bool bNearBorders;
        /// node graph index used for model indexing
        size_t nGraphNodeIdx;
        /// map index used for model indexing (raw, using only grid coords)
        size_t nMapIdx;
        /// LUT index used for model indexing (raw, using all coords)
        size_t nLUTIdx;
        /// camera head index associated with this node (for stereo, always the same)
        size_t nCamIdx;
        /// temporal depth layer index associated with this node (0 = current frame, positive values = past offset)
        size_t nLayerIdx;
        /// id for this node's unary factor (not SIZE_MAX only if valid)
        size_t nUnaryFactID;
        /// pointer to this node's unary function (non-null only if valid)
        ExplicitFunction* pUnaryFunc;
        /// weights for this node's pairwise costs (mutable for possible updates during inference)
        mutable std::array<float,s_nPairwOrients> afPairwWeights;
        /// vector of pointers to all (valid) cliques owned by this node as 1st member (all must evaluate to true)
        lv::AutoBuffer<Clique*,4> vpCliques;
        /// array of pairwise cliques owned by this node as 1st member (evaluates to true only if valid)
        std::array<PairwClique,s_nPairwOrients> aPairwCliques;
        /// LUT of membership pairs (global clique idx + internal node idx) for all cliques this node belongs to
        lv::AutoBuffer<std::pair<IndexType,IndexType>,4> vCliqueMemberLUT;
    };

    /// basic info struct used for node-level stereo graph model updates and data lookups
    struct StereoNodeInfo : NodeInfo {
        /// epipolar clique owned by this node as 1st member (evaluates to true only if valid)
        EpipolarClique oEpipolarClique;
    };

    /// basic info struct used for node-level resegm graph model updates and data lookups
    struct ResegmNodeInfo : NodeInfo {
        /// stacked (multi-layer) map element index associated with this node
        size_t nStackedIdx;
        /// temporal clique owned by this node as 1st member (evaluates to true only if valid)
        TemporalClique oTemporalClique;
    };

} // anonymous namespace

/// holds graph model data for both stereo and resegmentation models
struct SegmMatcher::GraphModelData {
    /// default constructor; receives model construction data from algo constructor
    GraphModelData(const CamArray<cv::Mat>& aROIs, const std::vector<OutputLabelType>& vRealStereoLabels, size_t nStereoLabelStep, size_t nPrimaryCamIdx);
    /// (pre)calculates features required for model updates, and optionally returns them in packet format
    void calcFeatures(const MatArrayIn& aInputs, cv::Mat* pFeaturesPacket=nullptr);
    /// sets a previously precalculated features packet to be used in the next model updates (do not modify it before that!)
    void setNextFeatures(const cv::Mat& oPackedFeatures);
    /// performs the actual bi-model, bi-spectral inference
    opengm::InferenceTermination infer();
    /// translate an internal graph label to a real disparity offset label
    OutputLabelType getRealLabel(InternalLabelType nLabel) const;
    /// translate a real disparity offset label to an internal graph label
    InternalLabelType getInternalLabel(OutputLabelType nRealLabel) const;
    /// returns the offset value to use for indexing pixels in the other camera image
    int getOffsetValue(size_t nCamIdx, InternalLabelType nLabel) const;
    /// returns the offset column to use for indexing pixels in the other camera image
    int getOffsetColIdx(size_t nCamIdx, int nColIdx, InternalLabelType nLabel) const;
    /// returns the stereo associations count for a given graph node by row/col indices
    AssocCountType getAssocCount(int nRowIdx, int nColIdx) const;
    /// returns the cost of adding a stereo association for a given node coord set & origin column idx
    ValueType calcAddAssocCost(int nRowIdx, int nColIdx, InternalLabelType nLabel) const;
    /// returns the cost of removing a stereo association for a given node coord set & origin column idx
    ValueType calcRemoveAssocCost(int nRowIdx, int nColIdx, InternalLabelType nLabel) const;
    /// returns the total stereo association cost for all grid nodes
    ValueType calcTotalAssocCost() const;
    /// helper func to display segmentation maps
    cv::Mat getResegmMapDisplay(size_t nLayerIdx, size_t nCamIdx) const;
    /// helper func to display scaled disparity maps
    cv::Mat getStereoDispMapDisplay(size_t nLayerIdx, size_t nCamIdx) const;
    /// helper func to display scaled assoc count maps (for primary cam only)
    cv::Mat getAssocCountsMapDisplay() const;

    /// number of frame sets processed so far (used to toggle temporal links on/off)
    size_t m_nFramesProcessed;
    /// max move making iteration count allowed during stereo/resegm inference
    size_t m_nMaxStereoMoveCount,m_nMaxResegmMoveCount;
    /// random seeds to use to initialize labeling/label-order arrays
    size_t m_nStereoLabelOrderRandomSeed,m_nStereoLabelingRandomSeed;
    /// contains the (internal) stereo label ordering to use for each iteration
    std::vector<InternalLabelType> m_vStereoLabelOrdering;
    /// holds the set of features to use (or used) during the next (or past) inference (mutable, as shape features will change during inference)
    mutable TemporalArray<std::vector<cv::Mat>> m_avFeatures;
    /// contains the (internal) labelings of the stereo/resegm graph (mutable for inference)
    mutable cv::Mat_<InternalLabelType> m_oSuperStackedStereoLabeling,m_oSuperStackedResegmLabeling,m_oInitSuperStackedResegmLabeling;
    /// contains the (internal) labelings of the stereo/resegm graph (mutable for inference)
    mutable CamArray<cv::Mat_<InternalLabelType>> m_aStackedStereoLabelings,m_aStackedResegmLabelings; // note: these mats point to the super-stacked labeling version above
    /// contains the (internal) labelings of the stereo/resegm graph (mutable for inference)
    mutable TemporalArray<CamArray<cv::Mat_<InternalLabelType>>> m_aaStereoLabelings,m_aaResegmLabelings; // note: these mats point to the stacked labeling version above
    /// 2d map which contain how many associations a graph node possesses (mutable for inference)
    mutable cv::Mat_<AssocCountType> m_oAssocCounts;
    /// 3d map which list the associations (by idx) for each graph node (mutable for inference)
    mutable cv::Mat_<AssocIdxType> m_oAssocMap;
    /// 2d map which contains transient unary factor labeling costs for all stereo/resegm graph nodes (mutable for inference)
    mutable cv::Mat_<ValueType> m_oStereoUnaryCosts,m_oResegmUnaryCosts;
    /// contains the ROIs used for grid setup passed in the constructor
    const CamArray<cv::Mat_<uchar>> m_aROIs;
    /// contains the predetermined (max) 2D grid size for the graph models
    const lv::MatSize m_oGridSize;
    /// contains all 'real' stereo labels, plus the 'occluded'/'dontcare' labels
    const std::vector<OutputLabelType> m_vStereoLabels;
    /// total number of (re)segmentation labels
    static constexpr size_t s_nResegmLabels = 2u;
    /// number of 'real' (i.e. non-reserved) stereo disparity labels
    const size_t m_nRealStereoLabels;
    /// total number of stereo disparity labels (including reserved ones)
    const size_t m_nStereoLabels;
    /// contains the step size between stereo labels (i.e. the disparity granularity)
    const size_t m_nDispOffsetStep;
    /// contains the minimum disparity offset value
    const size_t m_nMinDispOffset;
    /// contains the maximum disparity offset value
    const size_t m_nMaxDispOffset;
    /// primary camera index for stereo disparity estimation base
    const size_t m_nPrimaryCamIdx;
    /// internal label used for 'dont care' labeling
    const InternalLabelType m_nDontCareLabelIdx;
    /// internal label used for 'occluded' labeling
    const InternalLabelType m_nOccludedLabelIdx;
    /// opengm stereo graph model object
    std::unique_ptr<StereoModelType> m_pStereoModel;
    /// opengm resegm graph model object
    std::unique_ptr<ResegmModelType> m_pResegmModel;
    /// contains the eroded ROIs used for valid descriptor lookups
    CamArray<cv::Mat_<uchar>> m_aDescROIs;
    /// contains the super-stacked ROI used for grid setup passed in the constructor (for temporal ops)
    cv::Mat_<uchar> m_oSuperStackedROI;
    /// contains stacked ROIs used for grid setup passed in the constructor (for temporal ops)
    CamArray<cv::Mat_<uchar>> m_aStackedROIs; // note: these mats point to the super-stacked version above
    /// indices of valid nodes in the stereo/resegm graphs (based on primary ROI)
    std::vector<size_t> m_vStereoGraphIdxToMapIdxLUT,m_vResegmGraphIdxToMapIdxLUT;
    /// number of valid nodes/cliques in the stereo/resegm graphs (based on primary ROI)
    size_t m_nValidStereoGraphNodes,m_nValidResegmGraphNodes,m_nStereoCliqueCount,m_nResegmCliqueCount;
    /// stereo model info lookup array
    std::vector<StereoNodeInfo> m_vStereoNodeMap;
    /// resegm model info lookup array
    std::vector<ResegmNodeInfo> m_vResegmNodeMap;
    /// graph model factor counts used for validation in debug mode
    size_t m_nStereoUnaryFactCount,m_nStereoPairwFactCount,m_nStereoEpipolarFactCount;
    /// graph model factor counts used for validation in debug mode
    size_t m_nResegmUnaryFactCount,m_nResegmPairwFactCount,m_nResegmTemporalFactCount;
    /// stereo/resegm model pairwise function ids (for shared base lookups without weights)
    FuncIdentifType m_oStereoPairwFuncID_base,m_oResegmPairwFuncID_base;
    /// functions data arrays (contiguous blocks for all factors)
    std::unique_ptr<ValueType[]> m_aStereoFuncsData,m_aResegmFuncsData;
    /// stereo model unary/pairw/epipolar functions base pointers
    ValueType *m_pStereoUnaryFuncsDataBase,*m_pStereoPairwFuncsDataBase,*m_pStereoEpipolarFuncsDataBase,*m_pStereoFuncsDataEnd;
    /// resegm model unary/pairw/temporal functions base pointers
    ValueType *m_pResegmUnaryFuncsDataBase,*m_pResegmPairwFuncsDataBase,*m_pResegmTemporalFuncsDataBase,*m_pResegmFuncsDataEnd;
    /// gmm fg/bg models used for intra-spectral visual-data-based segmentation (3ch)
    CamArray<lv::GMM<SEGMMATCH_DEFAULT_GMM_3CH_COMPONENTS,3>> m_aFGModels_3ch,m_aBGModels_3ch;
    /// gmm fg/bg models used for intra-spectral visual-data-based segmentation (1ch)
    CamArray<lv::GMM<SEGMMATCH_DEFAULT_GMM_1CH_COMPONENTS,1>> m_aFGModels_1ch,m_aBGModels_1ch;
    /// gmm label component maps used for model learning (kept here to avoid reallocs)
    CamArray<cv::Mat_<int>> m_aGMMCompAssignMap; // note: maps contain the temporal depth as stacked layers
    /// cost lookup table for adding/removing/summing associations
    lv::AutoBuffer<ValueType,200> m_aAssocCostRealAddLUT,m_aAssocCostRealRemLUT,m_aAssocCostRealSumLUT;
    /// cost lookup table for approximately (worse case) adding/removing associations
    lv::AutoBuffer<ValueType,200> m_aAssocCostApproxAddLUT,m_aAssocCostApproxRemLUT;
    /// gradient factor lookup table for label similarity
    lv::LUT<uchar,float,256> m_aLabelSimCostGradFactLUT;
    /// temporary features vectors used as swap buffers for precomputation
    std::vector<cv::Mat> m_vTempFeatures,m_vLoadedFeatures;
    /// label lists used in median label blob analysis (updated along with shape maps)
    CamArray<std::vector<InternalLabelType>> m_avMedianShapeLabels;
    /// shape maps linking each foreground blob to a median shape label
    CamArray<cv::Mat_<int>> m_aShapeMaps;
    /// occlusion map that highlights which pixel is occluded in each disparity map
    CamArray<cv::Mat_<uchar>> m_aOcclusionMaps;
    /// inter-cam vote map used for parallel ops (large prealloc)
    cv::Mat_<int> m_oStereoVoteMap;

    /// holds the feature extractor to use on input images
#if SEGMMATCH_CONFIG_USE_DASCGF_AFFINITY || SEGMMATCH_CONFIG_USE_DASCRF_AFFINITY
    std::unique_ptr<DASC> m_pImgDescExtractor;
#elif SEGMMATCH_CONFIG_USE_LSS_AFFINITY
    std::unique_ptr<LSS> m_pImgDescExtractor;
#elif SEGMMATCH_CONFIG_USE_MI_AFFINITY
    std::unique_ptr<MutualInfo> m_pImgDescExtractor; // although not really a 'descriptor' extractor...
#endif //SEGMMATCH_CONFIG_USE_..._AFFINITY
    /// holds the feature extractor to use on input shapes
    std::unique_ptr<ShapeContext> m_pShpDescExtractor;
    /// defines the minimum grid border size based on the feature extractors used
    size_t m_nGridBorderSize;
    /// holds the last/next features packet info vector
    std::vector<lv::MatInfo> m_vExpectedFeatPackInfo,m_vLatestFeatPackInfo;
    /// holds the latest input image/mask matrices (vertically stacked for each layer, for each camera head)
    CamArray<cv::Mat> m_aStackedInputImages,m_aStackedInputMasks;
    /// holds the latest input image/mask matrices (note: mats point to stacked versions above)
    TemporalArray<MatArrayIn> m_aaInputs;
    /// defines whether the next model update should use precalc features
    bool m_bUsePrecalcFeaturesNext;
    /// used for debug only; passed from top-level algo when available
    lv::DisplayHelperPtr m_pDisplayHelper;

protected:
    /// adds a stereo association for a given node coord set & origin column idx
    void addAssoc(int nRowIdx, int nColIdx, InternalLabelType nLabel) const;
    /// removes a stereo association for a given node coord set & origin column idx
    void removeAssoc(int nRowIdx, int nColIdx, InternalLabelType nLabel) const;
    /// builds a stereo graph model using ROI information
    void buildStereoModel();
    /// updates a stereo graph model using new features data
    void updateStereoModel(bool bInit);
    /// resets primary+secondary stereo graph labelings using current model data
    void resetStereoLabelings();
    /// resets a secondary stereo graph labeling by projecting the primary disparity map data
    void resetStereoLabelingByProjection(size_t nCamIdx);
    /// computes the median labelings for each foreground shape in the disparity maps
    void computeMedianLabelings(size_t nTargetCamIdx=SIZE_MAX/*do both by default*/);
    /// computes the occlusion maps for a target camera (or both)
    void computeOcclusionMaps(size_t nTargetCamIdx=SIZE_MAX/*do both by default*/);

    /// builds a resegm graph model using ROI information
    void buildResegmModel();
    /// updates a shape graph model using new features data
    void updateResegmModel(bool bInit);
    /// calculates image features required for model updates using the provided input image array
    void calcImageFeatures(const CamArray<cv::Mat>& aInputImages, std::vector<cv::Mat>& vFeatures);
    /// calculates shape features required for model updates using the provided input mask array
    void calcShapeFeatures(const CamArray<cv::Mat_<InternalLabelType>>& aInputMasks, std::vector<cv::Mat>& vFeatures);
    /// calculates shape mask distance features required for model updates using the provided input mask & camera index
    void calcShapeDistFeatures(const cv::Mat_<InternalLabelType>& oInputMask, size_t nCamIdx, std::vector<cv::Mat>& vFeatures);
    /// initializes foreground and background GMM parameters via KNN using the given image and mask (where all values >0 are considered foreground)
    void initGaussianMixtureParams(const cv::Mat& oInput, const cv::Mat& oMask, const cv::Mat& oROI, size_t nCamIdx);
    /// assigns each input image pixel its most likely GMM component in the output map, using the BG or FG model as dictated by the input mask
    void assignGaussianMixtureComponents(const cv::Mat& oInput, const cv::Mat& oMask, cv::Mat& oAssignMap, const cv::Mat& oROI, size_t nCamIdx);
    /// learns the ideal foreground and background GMM parameters to fit the components assigned to the pixels of the input image
    void learnGaussianMixtureParams(const cv::Mat& oInput, const cv::Mat& oMask, const cv::Mat& oAssignMap, const cv::Mat& oROI, size_t nCamIdx);
    /// evaluates foreground segmentation probability density for a given image & lookup index
    double getGMMFGProb(const cv::Mat& oInput, size_t nElemIdx, size_t nCamIdx) const;
    /// evaluates background segmentation probability density for a given image & lookup index
    double getGMMBGProb(const cv::Mat& oInput, size_t nElemIdx, size_t nCamIdx) const;
    /// calculates a stereo unary move cost for a single graph node
    ValueType calcStereoUnaryMoveCost(size_t nGraphNodeIdx, InternalLabelType nOldLabel, InternalLabelType nNewLabel) const;
    /// fill internal temporary energy cost mats for the given stereo move operation
    void calcStereoMoveCosts(InternalLabelType nNewLabel) const;
    /// fill internal temporary energy cost mats for the given resegm move operation
    void calcResegmMoveCosts(InternalLabelType nNewLabel) const;
#if (SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF)
    /// init minimizer for later inference using SoSPD (returns active clique count)
    template<typename TNode>
    size_t initMinimizer(sospd::SubmodularIBFS<ValueType,IndexType>& oMinimizer,
                         const std::vector<TNode>& vNodeMap,
                         const std::vector<size_t>& vGraphIdxToMapIdxLUT);
    /// setup graph, dual, and cliques for later inference using SoSPD (returns active clique count)
    template<typename TFunc, typename TNode>
    size_t setupPrimalDual(const std::vector<TNode>& vNodeMap,
                           const std::vector<size_t>& vGraphIdxToMapIdxLUT,
                           const cv::Mat_<InternalLabelType>& oLabeling,
                           cv::Mat_<ValueType>& oDualMap,
                           cv::Mat_<ValueType>& oHeightMap,
                           size_t nTotLabels, size_t nMaxCliques);
    /// solves a move operation using the SoSPD algo of Fix et al.; see "A Primal-Dual Algorithm for Higher-Order Multilabel Markov Random Fields" in CVPR2014 for more info
    template<typename TFunc, typename TNode>
    void solvePrimalDual(sospd::SubmodularIBFS<ValueType,IndexType>& oMinimizer,
                         const std::vector<TNode>& vNodeMap,
                         const std::vector<size_t>& vGraphIdxToMapIdxLUT,
                         const cv::Mat_<ValueType>& oUnaryCostMap,
                         cv::Mat_<InternalLabelType>& oLabeling,
                         cv::Mat_<ValueType>& oDualMap,
                         cv::Mat_<ValueType>& oHeightMap,
                         InternalLabelType nAlphaLabel,
                         size_t nTotLabels,
                         bool bUpdateAssocs,
                         TemporalArray<CamArray<size_t>>& aanChangedLabels);
    cv::Mat_<ValueType> m_oStereoDualMap,m_oStereoHeightMap,m_oResegmDualMap,m_oResegmHeightMap;
#endif //(SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF)
    /// holds stereo disparity graph inference algorithm interface (redirects for bi-model inference)
    std::unique_ptr<StereoGraphInference> m_pStereoInf;
    /// holds resegmentation graph inference algorithm interface (redirects for bi-model inference)
    std::unique_ptr<ResegmGraphInference> m_pResegmInf;
};

/// algo interface for multi-label graph model inference
struct SegmMatcher::StereoGraphInference : opengm::Inference<StereoModelType,opengm::Minimizer> {
    /// full constructor of the inference algorithm; the graphical model must have already been constructed prior to this call
    StereoGraphInference(GraphModelData& oData);
    /// returns the name of this inference method, for debugging/identification purposes
    virtual std::string name() const override;
    /// returns a copy of the internal const reference to the graphical model to solve
    virtual const StereoModelType& graphicalModel() const override;
    /// redirects inference to the bi-model implementation
    virtual opengm::InferenceTermination infer() override;
    /// sets an internal labeling starting point for the inference (the iterator must be valid over its 'nNodes' next values)
    virtual void setStartingPoint(typename std::vector<InternalLabelType>::const_iterator begin) override;
    /// translates and sets a labeling starting point for the inference (must be the same size as the internal graph model grid)
    virtual void setStartingPoint(const cv::Mat_<OutputLabelType>& oLabeling);
    /// returns the internal labeling solution, as determined by solving the graphical model inference problem
    virtual opengm::InferenceTermination arg(std::vector<InternalLabelType>& oLabeling, const size_t n=1) const override;
    /// returns the current real labeling solution, as determined by solving+translating the graphical model inference problem
    virtual void getOutput(cv::Mat_<OutputLabelType>& oLabeling) const;
    /// returns the energy of the current labeling solution
    virtual ValueType value() const override;
    /// ref to SegmMatcher::m_pModelData
    GraphModelData& m_oData;
    /// camera head index targeted by the inference algo
    const size_t m_nPrimaryCamIdx;
};

/// algo interface for binary label graph model inference
struct SegmMatcher::ResegmGraphInference : opengm::Inference<ResegmModelType,opengm::Minimizer> {
    /// full constructor of the inference algorithm; the graphical model must have already been constructed prior to this call
    ResegmGraphInference(GraphModelData& oData);
    /// returns the name of this inference method, for debugging/identification purposes
    virtual std::string name() const override;
    /// returns a copy of the internal const reference to the graphical model to solve
    virtual const ResegmModelType& graphicalModel() const override;
    /// redirects inference to the bi-model implementation
    virtual opengm::InferenceTermination infer() override;
    /// sets an internal labeling starting point for the inference (the iterator must be valid over its 'nNodes' next values)
    virtual void setStartingPoint(typename std::vector<InternalLabelType>::const_iterator begin) override;
    /// translates and sets a labeling starting point for the inference (must be the same size as the internal graph model grid)
    virtual void setStartingPoint(const TemporalArray<CamArray<cv::Mat_<OutputLabelType>>>& aaLabeling);
    /// returns the internal labeling solution, as determined by solving the graphical model inference problem
    virtual opengm::InferenceTermination arg(std::vector<InternalLabelType>& oLabeling, const size_t n=1) const override;
    /// returns the current real labeling solution, as determined by solving+translating the graphical model inference problem
    virtual void getOutput(TemporalArray<CamArray<cv::Mat_<OutputLabelType>>>& aaLabeling) const;
    /// returns the energy of the current labeling solution
    virtual ValueType value() const override;
    /// ref to SegmMatcher::m_pModelData
    GraphModelData& m_oData;
};

namespace {

    /// stereo graph node iterator helper for std functions
    struct StereoGraphNodeIter {
        /// self-type used to identify iterator through traits
        typedef StereoGraphNodeIter self_type;
        /// deref'd iterator return type (hidden type)
        typedef StereoNodeInfo value_type;
        /// reference to deref'd iterator return type (hidden type)
        typedef StereoNodeInfo& reference;
        /// pointer to deref'd iterator return type (hidden type)
        typedef StereoNodeInfo* pointer;
        /// difference type used for random access distance lookups
        typedef int64_t difference_type;
        /// tag used to identify iterator type through traits
        typedef std::random_access_iterator_tag iterator_category;
        /// default iterator constructor, required by forward iterator
        explicit StereoGraphNodeIter(IndexType nInitGraphNodeIdx={})  :
                m_pData(nullptr),m_nGraphNodeIdx(nInitGraphNodeIdx) {}
        /// full iterator constructor, produces valid deref nodes if init is already in range
        explicit StereoGraphNodeIter(SegmMatcher::GraphModelData* pData, IndexType nInitGraphNodeIdx={}) :
                m_pData(pData),m_nGraphNodeIdx(nInitGraphNodeIdx) {
            lvDbgAssert(!m_pData || (m_pData->m_vStereoNodeMap.size()>=m_pData->m_nValidStereoGraphNodes));
        }
        /// default copy constructor, required by most iterator types
        StereoGraphNodeIter(const StereoGraphNodeIter&) = default;
        /// prefix increment operator, required by most iterator types
        self_type& operator++() {++m_nGraphNodeIdx; return *this;}
        /// postfix increment operator, required by input iterator
        self_type& operator++(int) {m_nGraphNodeIdx++; return *this;}
        /// prefix decrement operator, required by bidirectional iterator
        self_type& operator--() {--m_nGraphNodeIdx; return *this;}
        /// postfix decrement operator, required by bidirectional iterator
        self_type& operator--(int) {m_nGraphNodeIdx--; return *this;}
        /// equality operator, required by input iterator
        bool operator==(const self_type& rhs) const {return m_nGraphNodeIdx==rhs.m_nGraphNodeIdx;}
        /// inequality operator, required by input iterator
        bool operator!=(const self_type& rhs) const {return m_nGraphNodeIdx!=rhs.m_nGraphNodeIdx;}
        /// smaller-than operator, required by random access iterator
        bool operator<(const self_type& rhs) const {return m_nGraphNodeIdx<rhs.m_nGraphNodeIdx;}
        /// larger-than operator, required by random access iterator
        bool operator>(const self_type& rhs) const {return m_nGraphNodeIdx>rhs.m_nGraphNodeIdx;}
        /// smaller-than-or-equal operator, required by random access iterator
        bool operator<=(const self_type& rhs) const {return m_nGraphNodeIdx<=rhs.m_nGraphNodeIdx;}
        /// larger-than-or-equal operator, required by random access iterator
        bool operator>=(const self_type& rhs) const {return m_nGraphNodeIdx>=rhs.m_nGraphNodeIdx;}
        /// add-assign operator, required by random access iterator
        self_type& operator+=(difference_type rhs) {m_nGraphNodeIdx+=rhs; return *this;}
        /// add operator, required by random access iterator
        self_type operator+(difference_type rhs) const {return self_type(m_pData,m_nGraphNodeIdx+rhs);}
        /// subtract-assign operator, required by random access iterator
        self_type& operator-=(difference_type rhs) {m_nGraphNodeIdx-=rhs; return *this;}
        /// subtract operator, required by random access iterator
        self_type operator-(difference_type rhs) const {return self_type(m_pData,m_nGraphNodeIdx-rhs);}
        /// subtract operator, required by random access iterator
        difference_type operator-(const self_type& rhs) const {return difference_type(m_nGraphNodeIdx)-difference_type(rhs.m_nGraphNodeIdx);}
        /// dereference operator, required by input iterator
        reference operator*() {
            lvDbgAssert(m_pData && m_nGraphNodeIdx<m_pData->m_nValidStereoGraphNodes);
            return m_pData->m_vStereoNodeMap[m_pData->m_vStereoGraphIdxToMapIdxLUT[m_nGraphNodeIdx]];
        }
        /// dereference operator from pointer, required by input iterator
        pointer operator->() {
            lvDbgAssert(m_pData && m_nGraphNodeIdx<m_pData->m_nValidStereoGraphNodes);
            return &(m_pData->m_vStereoNodeMap[m_pData->m_vStereoGraphIdxToMapIdxLUT[m_nGraphNodeIdx]]);
        }
        /// lookup operator, required by random access iterator
        reference operator[](difference_type n) const {
            lvDbgAssert(m_pData && (difference_type(m_nGraphNodeIdx)+n)>0 && (difference_type(m_nGraphNodeIdx)+n)<difference_type(m_pData->m_nValidStereoGraphNodes));
            return m_pData->m_vStereoNodeMap[m_pData->m_vStereoGraphIdxToMapIdxLUT[m_nGraphNodeIdx+n]];
        }
        SegmMatcher::GraphModelData* m_pData;
        IndexType m_nGraphNodeIdx;
    };

    /// resegm graph node iterator helper for std functions
    struct ResegmGraphNodeIter {
        /// self-type used to identify iterator through traits
        typedef ResegmGraphNodeIter self_type;
        /// deref'd iterator return type (hidden type)
        typedef ResegmNodeInfo value_type;
        /// reference to deref'd iterator return type (hidden type)
        typedef ResegmNodeInfo& reference;
        /// pointer to deref'd iterator return type (hidden type)
        typedef ResegmNodeInfo* pointer;
        /// difference type used for random access distance lookups
        typedef int64_t difference_type;
        /// tag used to identify iterator type through traits
        typedef std::random_access_iterator_tag iterator_category;
        /// default iterator constructor, required by forward iterator
        explicit ResegmGraphNodeIter(IndexType nInitGraphNodeIdx={})  :
                m_pData(nullptr),m_nGraphNodeIdx(nInitGraphNodeIdx) {}
        /// full iterator constructor, produces valid deref nodes if init is already in range
        explicit ResegmGraphNodeIter(SegmMatcher::GraphModelData* pData, IndexType nInitGraphNodeIdx={}) :
                m_pData(pData),m_nGraphNodeIdx(nInitGraphNodeIdx) {
            lvDbgAssert(!m_pData || (m_pData->m_vResegmNodeMap.size()>=m_pData->m_nValidResegmGraphNodes));
        }
        /// default copy constructor, required by most iterator types
        ResegmGraphNodeIter(const ResegmGraphNodeIter&) = default;
        /// prefix increment operator, required by most iterator types
        self_type& operator++() {++m_nGraphNodeIdx; return *this;}
        /// postfix increment operator, required by input iterator
        self_type& operator++(int) {m_nGraphNodeIdx++; return *this;}
        /// prefix decrement operator, required by bidirectional iterator
        self_type& operator--() {--m_nGraphNodeIdx; return *this;}
        /// postfix decrement operator, required by bidirectional iterator
        self_type& operator--(int) {m_nGraphNodeIdx--; return *this;}
        /// equality operator, required by input iterator
        bool operator==(const self_type& rhs) const {return m_nGraphNodeIdx==rhs.m_nGraphNodeIdx;}
        /// inequality operator, required by input iterator
        bool operator!=(const self_type& rhs) const {return m_nGraphNodeIdx!=rhs.m_nGraphNodeIdx;}
        /// smaller-than operator, required by random access iterator
        bool operator<(const self_type& rhs) const {return m_nGraphNodeIdx<rhs.m_nGraphNodeIdx;}
        /// larger-than operator, required by random access iterator
        bool operator>(const self_type& rhs) const {return m_nGraphNodeIdx>rhs.m_nGraphNodeIdx;}
        /// smaller-than-or-equal operator, required by random access iterator
        bool operator<=(const self_type& rhs) const {return m_nGraphNodeIdx<=rhs.m_nGraphNodeIdx;}
        /// larger-than-or-equal operator, required by random access iterator
        bool operator>=(const self_type& rhs) const {return m_nGraphNodeIdx>=rhs.m_nGraphNodeIdx;}
        /// add-assign operator, required by random access iterator
        self_type& operator+=(difference_type rhs) {m_nGraphNodeIdx+=rhs; return *this;}
        /// add operator, required by random access iterator
        self_type operator+(difference_type rhs) const {return self_type(m_pData,m_nGraphNodeIdx+rhs);}
        /// subtract-assign operator, required by random access iterator
        self_type& operator-=(difference_type rhs) {m_nGraphNodeIdx-=rhs; return *this;}
        /// subtract operator, required by random access iterator
        self_type operator-(difference_type rhs) const {return self_type(m_pData,m_nGraphNodeIdx-rhs);}
        /// subtract operator, required by random access iterator
        difference_type operator-(const self_type& rhs) const {return difference_type(m_nGraphNodeIdx)-difference_type(rhs.m_nGraphNodeIdx);}
        /// dereference operator, required by input iterator
        reference operator*() {
            lvDbgAssert(m_pData && m_nGraphNodeIdx<m_pData->m_nValidResegmGraphNodes);
            return m_pData->m_vResegmNodeMap[m_pData->m_vResegmGraphIdxToMapIdxLUT[m_nGraphNodeIdx]];
        }
        /// dereference operator from pointer, required by input iterator
        pointer operator->() {
            lvDbgAssert(m_pData && m_nGraphNodeIdx<m_pData->m_nValidResegmGraphNodes);
            return &(m_pData->m_vResegmNodeMap[m_pData->m_vResegmGraphIdxToMapIdxLUT[m_nGraphNodeIdx]]);
        }
        /// lookup operator, required by random access iterator
        reference operator[](difference_type n) const {
            lvDbgAssert(m_pData && (difference_type(m_nGraphNodeIdx)+n)>0 && (difference_type(m_nGraphNodeIdx)+n)<difference_type(m_pData->m_nValidResegmGraphNodes));
            return m_pData->m_vResegmNodeMap[m_pData->m_vResegmGraphIdxToMapIdxLUT[m_nGraphNodeIdx+n]];
        }
        SegmMatcher::GraphModelData* m_pData;
        IndexType m_nGraphNodeIdx;
    };

} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr OutputLabelType SegmMatcher::s_nDontCareLabel;
constexpr OutputLabelType SegmMatcher::s_nOccludedLabel;
constexpr OutputLabelType SegmMatcher::s_nForegroundLabel;
constexpr OutputLabelType SegmMatcher::s_nBackgroundLabel;
constexpr InternalLabelType SegmMatcher::s_nForegroundLabelIdx;
constexpr InternalLabelType SegmMatcher::s_nBackgroundLabelIdx;
constexpr size_t SegmMatcher::s_nCameraCount;

size_t SegmMatcher::getTemporalDepth() {return s_nTemporalCliqueDepth;}

SegmMatcher::SegmMatcher(size_t nMinDispOffset, size_t nMaxDispOffset) {
    static_assert(getInputStreamCount()==4 && getOutputStreamCount()==4 && getCameraCount()==2,"i/o stream must be two image-mask pairs");
    static_assert(getInputStreamCount()==InputPackSize && getOutputStreamCount()==OutputPackSize,"bad i/o internal enum mapping");
    lvDbgExceptionWatch;
    m_nDispStep = SEGMMATCH_DEFAULT_DISPARITY_STEP;
    lvAssert_(m_nDispStep>0,"specified disparity offset step size must be strictly positive");
    if(nMaxDispOffset<nMinDispOffset)
        std::swap(nMaxDispOffset,nMinDispOffset);
    nMaxDispOffset -= (nMaxDispOffset-nMinDispOffset)%m_nDispStep;
    lvAssert_(nMaxDispOffset<size_t(s_nOccludedLabel),"using reserved disparity integer label value");
    lvAssert_((nMaxDispOffset-nMinDispOffset)/m_nDispStep>size_t(0),"disparity range must not be null");
    lvAssert_(((nMaxDispOffset-nMinDispOffset)%m_nDispStep)==0,"irregular disparity range label count with given step size");
    const size_t nMaxAllowedDispLabelCount = size_t(std::numeric_limits<InternalLabelType>::max()-2);
    const size_t nExpectedDispLabelCount = ((nMaxDispOffset-nMinDispOffset)/m_nDispStep)+1; // +1 since max label is included in the range
    lvAssert__(nMaxAllowedDispLabelCount>=nExpectedDispLabelCount,"internal stereo label type too small for given disparity range (max = %d)",(int)nMaxAllowedDispLabelCount);
    m_vStereoLabels = lv::make_range((OutputLabelType)nMinDispOffset,(OutputLabelType)nMaxDispOffset,(OutputLabelType)m_nDispStep);
    lvDbgAssert(nExpectedDispLabelCount==m_vStereoLabels.size());
    lvAssert_(m_vStereoLabels.size()>1,"graph must have at least two possible output labels, beyond reserved ones");
}

SegmMatcher::~SegmMatcher() {}

void SegmMatcher::initialize(const std::array<cv::Mat,s_nCameraCount>& aROIs, size_t nPrimaryCamIdx) {
    static_assert(getCameraCount()==2,"bad static array size, mismatch with cam head count (hardcoded stuff below will break)");
    lvDbgExceptionWatch;
    lvAssert_(!aROIs[0].empty() && aROIs[0].total()>1 && aROIs[0].type()==CV_8UC1,"bad input ROI size/type");
    lvAssert_(lv::MatInfo(aROIs[0])==lv::MatInfo(aROIs[1]),"mismatched ROI size/type");
    lvAssert_(m_nDispStep>0,"specified disparity offset step size must be strictly positive");
    lvAssert_(m_vStereoLabels.size()>1,"graph must have at least two possible output labels, beyond reserved ones");
    lvAssert_(nPrimaryCamIdx<getCameraCount(),"primary camera idx is out of range");
    m_pModelData = std::make_unique<GraphModelData>(aROIs,m_vStereoLabels,m_nDispStep,nPrimaryCamIdx);
    if(m_pDisplayHelper)
        m_pModelData->m_pDisplayHelper = m_pDisplayHelper;
}

void SegmMatcher::apply(const MatArrayIn& aInputs, MatArrayOut& aOutputs) {
    static_assert(s_nInputArraySize==4 && getCameraCount()==2,"lots of hardcoded indices below");
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    const size_t nLayerSize = m_pModelData->m_oGridSize.total();
    for(size_t nInputIdx=0u; nInputIdx<aInputs.size(); ++nInputIdx)
        lvAssert__(m_pModelData->m_oGridSize==aInputs[nInputIdx].size,"input in array at index=%d had the wrong size",(int)nInputIdx);
    for(size_t nCamIdx=0u; nCamIdx<getCameraCount(); ++nCamIdx) {
        const cv::Mat& oInputImg = aInputs[nCamIdx*InputPackOffset+InputPackOffset_Img];
        const cv::Mat& oInputMask = aInputs[nCamIdx*InputPackOffset+InputPackOffset_Mask];
        lvAssert_(oInputImg.type()==CV_8UC1 || oInputImg.type()==CV_8UC3,"unexpected input image type");
        lvAssert_(oInputMask.type()==CV_8UC1,"unexpected input mask type");
        lvAssert_((cv::countNonZero(oInputMask==0)+cv::countNonZero(oInputMask==255))==(int)oInputMask.total(),"input mask must be binary (0 or 255 only)");
        if(m_pModelData->m_aStackedInputImages[nCamIdx].empty() || oInputImg.type()!=m_pModelData->m_aStackedInputImages[nCamIdx].type()) {
            m_pModelData->m_aStackedInputImages[nCamIdx].create(oInputImg.rows*(int)getTemporalLayerCount(),oInputImg.cols,oInputImg.type());
            for(size_t nLayerIdx=0u; nLayerIdx<getTemporalLayerCount(); ++nLayerIdx)
                m_pModelData->m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Img] = cv::Mat(oInputImg.size(),oInputImg.type(),m_pModelData->m_aStackedInputImages[nCamIdx].data+nLayerSize*oInputImg.channels()*nLayerIdx);
        }
    }
    lvAssert_(!m_pModelData->m_bUsePrecalcFeaturesNext || m_pModelData->m_vExpectedFeatPackInfo.size()==FeatPackSize,"unexpected precalculated features vec size");
    for(size_t nLayerIdx=getTemporalLayerCount()-1u; nLayerIdx>0; --nLayerIdx) {
        // copy over all 'old' inputs by sliding one layer at a time
        for(size_t nInputIdx=0u; nInputIdx<aInputs.size(); ++nInputIdx)
            m_pModelData->m_aaInputs[nLayerIdx-1u][nInputIdx].copyTo(m_pModelData->m_aaInputs[nLayerIdx][nInputIdx]);
        for(size_t nCamIdx=0u; nCamIdx<getCameraCount(); ++nCamIdx) {
            m_pModelData->m_aaStereoLabelings[nLayerIdx-1u][nCamIdx].copyTo(m_pModelData->m_aaStereoLabelings[nLayerIdx][nCamIdx]);
            m_pModelData->m_aaResegmLabelings[nLayerIdx-1u][nCamIdx].copyTo(m_pModelData->m_aaResegmLabelings[nLayerIdx][nCamIdx]);
        }
        std::swap(m_pModelData->m_avFeatures[nLayerIdx],m_pModelData->m_avFeatures[nLayerIdx-1u]);
    }
    for(size_t nInputIdx=0u; nInputIdx<aInputs.size(); ++nInputIdx) // copy new inputs to first layer
        aInputs[nInputIdx].copyTo(m_pModelData->m_aaInputs[0][nInputIdx]);
    if(m_pModelData->m_bUsePrecalcFeaturesNext) {
        m_pModelData->m_bUsePrecalcFeaturesNext = false;
        lvDbgAssert(m_pModelData->m_vLoadedFeatures.size()==FeatPackSize);
        std::swap(m_pModelData->m_vLoadedFeatures,m_pModelData->m_avFeatures[0]);
    }
    else {
        m_pModelData->calcFeatures(m_pModelData->m_aaInputs[0]);
        lvDbgAssert(m_pModelData->m_vTempFeatures.size()==FeatPackSize);
        std::swap(m_pModelData->m_vTempFeatures,m_pModelData->m_avFeatures[0]);
    }
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) // only overwrite current resegm labeling temporal layer
        cv::Mat(((m_pModelData->m_aaInputs[0][nCamIdx*InputPackOffset+InputPackOffset_Mask]>0)&m_pModelData->m_aROIs[nCamIdx])&s_nForegroundLabelIdx).copyTo(m_pModelData->m_aaResegmLabelings[0][nCamIdx]);
    if(m_pModelData->m_nFramesProcessed==0u) {
        for(size_t nLayerIdx=1u; nLayerIdx<getTemporalLayerCount(); ++nLayerIdx) {
            for(size_t nInputIdx=0u; nInputIdx<aInputs.size(); ++nInputIdx) // copy initial inputs to all layers
                m_pModelData->m_aaInputs[0][nInputIdx].copyTo(m_pModelData->m_aaInputs[nLayerIdx][nInputIdx]);
            for(size_t nFeatsIdx=0; nFeatsIdx<m_pModelData->m_avFeatures[0].size(); ++nFeatsIdx) // copy initial features to all layers
                m_pModelData->m_avFeatures[0][nFeatsIdx].copyTo(m_pModelData->m_avFeatures[nLayerIdx][nFeatsIdx]);
            for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) // copy initial resegm labeling to all layers
                m_pModelData->m_aaResegmLabelings[0][nCamIdx].copyTo(m_pModelData->m_aaResegmLabelings[nLayerIdx][nCamIdx]);
        }
    }
    m_pModelData->infer();
    ++m_pModelData->m_nFramesProcessed;
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        // copy over latest labelings as output; note: the segm masks may change over future iterations --- user will have to revalidate
        m_pModelData->m_aaStereoLabelings[0][nCamIdx].copyTo(aOutputs[nCamIdx*OutputPackOffset+OutputPackOffset_Disp]);
        m_pModelData->m_aaResegmLabelings[0][nCamIdx].copyTo(aOutputs[nCamIdx*OutputPackOffset+OutputPackOffset_Mask]);
    }
}

void SegmMatcher::getOutput(size_t nTemporalLayerIdx, MatArrayOut& aOutputs) const {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    lvAssert_(m_pModelData->m_nFramesProcessed>0u,"model must have processed at least one frame");
    lvAssert_(nTemporalLayerIdx<getTemporalLayerCount(),"temporal layer index out of range");
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        m_pModelData->m_aaStereoLabelings[nTemporalLayerIdx][nCamIdx].copyTo(aOutputs[nCamIdx*OutputPackOffset+OutputPackOffset_Disp]);
        m_pModelData->m_aaResegmLabelings[nTemporalLayerIdx][nCamIdx].copyTo(aOutputs[nCamIdx*OutputPackOffset+OutputPackOffset_Mask]);
    }
}

void SegmMatcher::calcFeatures(const MatArrayIn& aInputs, cv::Mat* pFeaturesPacket) {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    m_pModelData->calcFeatures(aInputs,pFeaturesPacket);
}

void SegmMatcher::setNextFeatures(const cv::Mat& oPackedFeatures) {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    m_pModelData->setNextFeatures(oPackedFeatures);
}

void SegmMatcher::resetTemporalModel() {
    lvDbgExceptionWatch;
    m_pModelData->m_nFramesProcessed = 0u;
}

std::string SegmMatcher::getFeatureExtractorName() const {
#if SEGMMATCH_CONFIG_USE_DASCGF_AFFINITY
    return "sc-dasc-gf";
#elif SEGMMATCH_CONFIG_USE_DASCRF_AFFINITY
    return "sc-dasc-rf";
#elif SEGMMATCH_CONFIG_USE_LSS_AFFINITY
    return "sc-lss";
#elif SEGMMATCH_CONFIG_USE_MI_AFFINITY
    return "sc-mi";
#elif SEGMMATCH_CONFIG_USE_SSQDIFF_AFFINITY
    return "sc-ssqrdiff";
#endif //SEGMMATCH_CONFIG_USE_..._AFFINITY
}

size_t SegmMatcher::getMaxLabelCount() const {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    return m_pModelData->m_vStereoLabels.size();
}

const std::vector<SegmMatcher::OutputLabelType>& SegmMatcher::getLabels() const {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    return m_pModelData->m_vStereoLabels;
}

cv::Mat SegmMatcher::getResegmMapDisplay(size_t nLayerIdx, size_t nCamIdx) const {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    return m_pModelData->getResegmMapDisplay(nLayerIdx,nCamIdx);
}

cv::Mat SegmMatcher::getStereoDispMapDisplay(size_t nLayerIdx, size_t nCamIdx) const {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    return m_pModelData->getStereoDispMapDisplay(nLayerIdx,nCamIdx);
}

cv::Mat SegmMatcher::getAssocCountsMapDisplay() const {
    lvDbgExceptionWatch;
    lvAssert_(m_pModelData,"model must be initialized first");
    return m_pModelData->getAssocCountsMapDisplay();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr size_t SegmMatcher::GraphModelData::s_nResegmLabels;

SegmMatcher::GraphModelData::GraphModelData(const CamArray<cv::Mat>& aROIs, const std::vector<OutputLabelType>& vRealStereoLabels, size_t nStereoLabelStep, size_t nPrimaryCamIdx) :
        m_nFramesProcessed(0u),
        m_nMaxStereoMoveCount(SEGMMATCH_DEFAULT_MAX_STEREO_ITER),
        m_nMaxResegmMoveCount(SEGMMATCH_DEFAULT_MAX_RESEGM_ITER),
        m_nStereoLabelOrderRandomSeed(0u),
        m_nStereoLabelingRandomSeed(0u),
        m_aROIs(CamArray<cv::Mat_<uchar>>{aROIs[0]>0,aROIs[1]>0}),
        m_oGridSize(m_aROIs[0].size()),
        m_vStereoLabels(lv::concat<OutputLabelType>(vRealStereoLabels,std::vector<OutputLabelType>{s_nDontCareLabel,s_nOccludedLabel})),
        m_nRealStereoLabels(vRealStereoLabels.size()),
        m_nStereoLabels(vRealStereoLabels.size()+2u),
        m_nDispOffsetStep(nStereoLabelStep),
        m_nMinDispOffset(size_t(m_vStereoLabels[0])),
        m_nMaxDispOffset(size_t(m_vStereoLabels.size()>3u?m_vStereoLabels[m_vStereoLabels.size()-3u]:m_vStereoLabels.back())),
        m_nPrimaryCamIdx(nPrimaryCamIdx),
        m_nDontCareLabelIdx(InternalLabelType(m_vStereoLabels.size()-2u)),
        m_nOccludedLabelIdx(InternalLabelType(m_vStereoLabels.size()-1u)),
        m_bUsePrecalcFeaturesNext(false) {
    static_assert(getCameraCount()==2,"bad static array size, hardcoded stuff in constr init list and below will break");
    lvDbgExceptionWatch;
    lvAssert_(m_nMaxStereoMoveCount>0u && m_nMaxResegmMoveCount>0u,"max iter counts must be strictly positive");
    for(size_t nCamIdx=0u; nCamIdx<getCameraCount(); ++nCamIdx) {
        lvAssert_(lv::MatInfo(m_aROIs[0])==lv::MatInfo(m_aROIs[nCamIdx]),"ROIs info must match");
        lvAssert_(cv::countNonZero(m_aROIs[nCamIdx]>0)>1,"ROIs must have at least two nodes");
    }
    lvAssert_(m_oGridSize.dims()==2 && m_oGridSize.total()>size_t(1),"graph grid must be 2D and have at least two nodes");
    lvAssert_(m_vStereoLabels.size()>3,"graph must have at least two possible output stereo labels, beyond reserved ones");
    lvAssert_(m_vStereoLabels.size()<=size_t(std::numeric_limits<InternalLabelType>::max()),"too many labels for internal type");
    lvDbgAssert(m_vStereoLabels[m_nDontCareLabelIdx]==s_nDontCareLabel && m_vStereoLabels[m_nOccludedLabelIdx]==s_nOccludedLabel);
    lvDbgAssert(std::min_element(m_vStereoLabels.begin(),m_vStereoLabels.begin()+m_vStereoLabels.size()-2)==m_vStereoLabels.begin() && m_vStereoLabels[0]>=OutputLabelType(0));
    lvDbgAssert(std::max_element(m_vStereoLabels.begin(),m_vStereoLabels.begin()+m_vStereoLabels.size()-2)==(m_vStereoLabels.begin()+m_vStereoLabels.size()-3));
    lvDbgAssert(std::equal(m_vStereoLabels.begin(),m_vStereoLabels.begin()+m_vStereoLabels.size()-2,lv::unique(m_vStereoLabels.begin(),m_vStereoLabels.end()).begin()+1));
    lvAssert_(m_nDispOffsetStep>0,"label step size must be positive");
    lvAssert_(m_oGridSize[1]>m_nMinDispOffset,"row length too small for smallest disp");
    lvAssert_(m_nMinDispOffset<m_nMaxDispOffset,"min/max disp offsets mismatch");
    lvAssert_(m_nPrimaryCamIdx<getCameraCount(),"bad primary camera index");
    lvDbgAssert_(std::numeric_limits<AssocCountType>::max()>m_oGridSize[1],"grid width is too large for association counter type");
#if SEGMMATCH_CONFIG_USE_DASCGF_AFFINITY
    m_pImgDescExtractor = std::make_unique<DASC>(DASC_DEFAULT_GF_RADIUS,DASC_DEFAULT_GF_EPS,DASC_DEFAULT_GF_SUBSPL,DASC_DEFAULT_PREPROCESS);
    const cv::Size oDescWinSize = m_pImgDescExtractor->windowSize();
    m_nGridBorderSize = (size_t)std::max(m_pImgDescExtractor->borderSize(0),m_pImgDescExtractor->borderSize(1));
#elif SEGMMATCH_CONFIG_USE_DASCRF_AFFINITY
    m_pImgDescExtractor = std::make_unique<DASC>(DASC_DEFAULT_RF_SIGMAS,DASC_DEFAULT_RF_SIGMAR,DASC_DEFAULT_RF_ITERS,DASC_DEFAULT_PREPROCESS);
    const cv::Size oDescWinSize = m_pImgDescExtractor->windowSize();
    m_nGridBorderSize = (size_t)std::max(m_pImgDescExtractor->borderSize(0),m_pImgDescExtractor->borderSize(1));
#elif SEGMMATCH_CONFIG_USE_LSS_AFFINITY
    const int nLSSInnerRadius = 0;
    const int nLSSOuterRadius = (int)SEGMMATCH_DEFAULT_LSSDESC_RAD;
    const int nLSSPatchSize = (int)SEGMMATCH_DEFAULT_LSSDESC_PATCH;
    const int nLSSAngBins = (int)SEGMMATCH_DEFAULT_LSSDESC_ANG_BINS;
    const int nLSSRadBins = (int)SEGMMATCH_DEFAULT_LSSDESC_RAD_BINS;
    m_pImgDescExtractor = std::make_unique<LSS>(nLSSInnerRadius,nLSSOuterRadius,nLSSPatchSize,nLSSAngBins,nLSSRadBins);
    const cv::Size oDescWinSize = m_pImgDescExtractor->windowSize();
    m_nGridBorderSize = (size_t)std::max(m_pImgDescExtractor->borderSize(0),m_pImgDescExtractor->borderSize(1));
#elif SEGMMATCH_CONFIG_USE_MI_AFFINITY
    const int nWindowSize = int(SEGMMATCH_DEFAULT_MI_WINDOW_RAD*2+1);
    const cv::Size oDescWinSize = cv::Size(nWindowSize,nWindowSize);
    m_nGridBorderSize = SEGMMATCH_DEFAULT_MI_WINDOW_RAD;
#elif SEGMMATCH_CONFIG_USE_SSQDIFF_AFFINITY
    constexpr int nSSqrDiffKernelSize = int(SEGMMATCH_DEFAULT_SSQDIFF_PATCH);
    const cv::Size oDescWinSize(nSSqrDiffKernelSize,nSSqrDiffKernelSize);
    m_nGridBorderSize = size_t(nSSqrDiffKernelSize/2);
#endif //SEGMMATCH_CONFIG_USE_..._AFFINITY
    const size_t nShapeContextInnerRadius = 2;
    const size_t nShapeContextOuterRadius = SEGMMATCH_DEFAULT_SCDESC_WIN_RAD;
    const size_t nShapeContextAngBins = SEGMMATCH_DEFAULT_SCDESC_ANG_BINS;
    const size_t nShapeContextRadBins = SEGMMATCH_DEFAULT_SCDESC_RAD_BINS;
    m_pShpDescExtractor = std::make_unique<ShapeContext>(nShapeContextInnerRadius,nShapeContextOuterRadius,nShapeContextAngBins,nShapeContextRadBins);
    lvAssert__(oDescWinSize.width<=(int)m_oGridSize[1] && oDescWinSize.height<=(int)m_oGridSize[0],"image is too small to compute descriptors with current pattern size -- need at least (%d,%d) and got (%d,%d)",oDescWinSize.width,oDescWinSize.height,(int)m_oGridSize[1],(int)m_oGridSize[0]);
    lvDbgAssert(m_nGridBorderSize<m_oGridSize[0] && m_nGridBorderSize<m_oGridSize[1]);
    lvDbgAssert(m_nGridBorderSize<(size_t)oDescWinSize.width && m_nGridBorderSize<(size_t)oDescWinSize.height);
    lvDbgAssert((size_t)std::max(m_pShpDescExtractor->borderSize(0),m_pShpDescExtractor->borderSize(1))<=m_nGridBorderSize);
    m_aAssocCostRealAddLUT.resize_static();
    m_aAssocCostRealRemLUT.resize_static();
    m_aAssocCostRealSumLUT.resize_static();
    m_aAssocCostApproxAddLUT.resize_static();
    m_aAssocCostApproxRemLUT.resize_static();
    lvDbgAssert(m_aAssocCostRealAddLUT.size()==m_aAssocCostRealSumLUT.size() && m_aAssocCostRealRemLUT.size()==m_aAssocCostRealSumLUT.size());
    lvDbgAssert(m_aAssocCostApproxAddLUT.size()==m_aAssocCostRealAddLUT.size() && m_aAssocCostApproxRemLUT.size()==m_aAssocCostRealRemLUT.size());
    lvDbgAssert_(m_nMaxDispOffset+m_nDispOffsetStep<m_aAssocCostRealSumLUT.size(),"assoc cost lut size might not be large enough");
    lvDbgAssert(SEGMMATCH_UNIQUE_COST_ZERO_COUNT<m_aAssocCostRealSumLUT.size());
    lvDbgAssert(SEGMMATCH_UNIQUE_COST_INCR_REL(0)==0.0f);
    std::fill_n(m_aAssocCostRealAddLUT.begin(),SEGMMATCH_UNIQUE_COST_ZERO_COUNT,cost_cast(0));
    std::fill_n(m_aAssocCostRealRemLUT.begin(),SEGMMATCH_UNIQUE_COST_ZERO_COUNT,cost_cast(0));
    std::fill_n(m_aAssocCostRealSumLUT.begin(),SEGMMATCH_UNIQUE_COST_ZERO_COUNT,cost_cast(0));
    for(size_t nIdx=SEGMMATCH_UNIQUE_COST_ZERO_COUNT; nIdx<m_aAssocCostRealAddLUT.size(); ++nIdx) {
        m_aAssocCostRealAddLUT[nIdx] = cost_cast(SEGMMATCH_UNIQUE_COST_INCR_REL(nIdx+1-SEGMMATCH_UNIQUE_COST_ZERO_COUNT)*SEGMMATCH_UNIQUE_COST_OVER_SCALE/m_nDispOffsetStep);
        m_aAssocCostRealRemLUT[nIdx] = -cost_cast(SEGMMATCH_UNIQUE_COST_INCR_REL(nIdx-SEGMMATCH_UNIQUE_COST_ZERO_COUNT)*SEGMMATCH_UNIQUE_COST_OVER_SCALE/m_nDispOffsetStep);
        m_aAssocCostRealSumLUT[nIdx] = ((nIdx==size_t(0))?cost_cast(0):(m_aAssocCostRealSumLUT[nIdx-1]+m_aAssocCostRealAddLUT[nIdx-1]));
    }
    for(size_t nIdx=0; nIdx<m_aAssocCostRealAddLUT.size(); ++nIdx) {
        // 'average' cost of removing one assoc from target pixel (not 'true' energy, but easy-to-optimize worse case)
        m_aAssocCostApproxRemLUT[nIdx] = ((nIdx==size_t(0))?cost_cast(0):cost_cast(-1.0f*m_aAssocCostRealSumLUT[nIdx]/nIdx+0.5f));
        if(m_nDispOffsetStep==size_t(1))
            // if m_nDispOffsetStep==1, then use real cost of adding one assoc to target pixel
            // (target can only have one new assoc per iter, due to single label move)
            m_aAssocCostApproxAddLUT[nIdx] = m_aAssocCostRealAddLUT[nIdx];
        else {
            // otherwise, use average cost of adding 'm_nDispOffsetStep' assocs to target block
            // (i.e. the average cost of adding the max possible number of new assocs to a block per iter)
            m_aAssocCostApproxAddLUT[nIdx] = cost_cast(0);
            for(size_t nOffsetIdx=nIdx; nOffsetIdx<nIdx+m_nDispOffsetStep; ++nOffsetIdx)
                m_aAssocCostApproxAddLUT[nIdx] += m_aAssocCostRealAddLUT[std::min(nOffsetIdx,m_aAssocCostRealAddLUT.size()-1)];
            m_aAssocCostApproxAddLUT[nIdx] = cost_cast(float(m_aAssocCostApproxAddLUT[nIdx])/m_nDispOffsetStep+0.5f);
        }
    }
    m_aLabelSimCostGradFactLUT.init(0,255,[](int nLocalGrad){
        return std::max(std::exp(float(SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST-nLocalGrad)/SEGMMATCH_LBLSIM_COST_GRADRAW_SCALE)-0.5f,0.0f);
    });
    lvDbgAssert(m_aLabelSimCostGradFactLUT.size()==size_t(256) && m_aLabelSimCostGradFactLUT.domain_offset_low()==0);
    lvDbgAssert(m_aLabelSimCostGradFactLUT.domain_index_step()==1.0 && m_aLabelSimCostGradFactLUT.domain_index_scale()==1.0);
    lvLog_(2,"\toutput disp labels:\n%s\n",lv::to_string(std::vector<OutputLabelType>(m_vStereoLabels.begin(),m_vStereoLabels.begin()+m_nRealStereoLabels)).c_str());
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    lvDbgAssert(nRows>1 && nCols>1);
    const size_t nTemporalLayerCount = getTemporalLayerCount();
    lvDbgAssert(nTemporalLayerCount>size_t(0));
    static_assert(getCameraCount()==2,"bad static array size, hardcoded stuff below will break");
    size_t nTotValidNodes = 0;
    CamArray<size_t> anValidGraphNodes = {};
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        anValidGraphNodes[nCamIdx] = (size_t)cv::countNonZero(m_aROIs[nCamIdx]);
        lvAssert(anValidGraphNodes[nCamIdx]<m_oGridSize.total());
        nTotValidNodes += anValidGraphNodes[nCamIdx];
        cv::erode(m_aROIs[nCamIdx],m_aDescROIs[nCamIdx],cv::getStructuringElement(cv::MORPH_RECT,oDescWinSize),cv::Point(-1,-1),1,cv::BORDER_CONSTANT,cv::Scalar_<uchar>::all(0));
    }
    const size_t nStereoUnaryFuncDataSize = anValidGraphNodes[m_nPrimaryCamIdx]*m_nStereoLabels;
    const size_t nStereoPairwFuncDataSize = 0u;/*removed 2018/02, now using scaled view arrays*/ //anValidGraphNodes[m_nPrimaryCamIdx]*s_nPairwOrients*(m_nStereoLabels*m_nStereoLabels);
    const size_t nStereoEpipolarFuncDataSize = SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN?(anValidGraphNodes[m_nPrimaryCamIdx]*(int)std::pow((int)m_nStereoLabels,(int)s_nEpipolarCliqueOrder)):size_t(0); // epipolar stride not taken into account here
    const size_t nStereoFuncDataSize = nStereoUnaryFuncDataSize+nStereoPairwFuncDataSize+nStereoEpipolarFuncDataSize;
    CamArray<size_t> anResegmUnaryFuncDataSize={},anResegmPairwFuncDataSize={},anResegmTemporalFuncDataSize={},anResegmFuncDataSize={};
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        anResegmUnaryFuncDataSize[nCamIdx] = anValidGraphNodes[nCamIdx]*s_nResegmLabels*nTemporalLayerCount;
        anResegmPairwFuncDataSize[nCamIdx] = anValidGraphNodes[nCamIdx]*s_nPairwOrients*(s_nResegmLabels*s_nResegmLabels)*nTemporalLayerCount;
        anResegmTemporalFuncDataSize[nCamIdx] = SEGMMATCH_CONFIG_USE_TEMPORAL_CONN?(anValidGraphNodes[nCamIdx]*(int)std::pow((int)s_nResegmLabels,(int)s_nTemporalCliqueOrder)):size_t(0); // temporal stride not taken into account here
        anResegmFuncDataSize[nCamIdx] = anResegmUnaryFuncDataSize[nCamIdx]+anResegmPairwFuncDataSize[nCamIdx]+anResegmTemporalFuncDataSize[nCamIdx];
    }
    const size_t nTotResegmUnaryFuncDataSize = std::accumulate(anResegmUnaryFuncDataSize.begin(),anResegmUnaryFuncDataSize.end(),size_t(0));
    const size_t nTotResegmPairwFuncDataSize = std::accumulate(anResegmPairwFuncDataSize.begin(),anResegmPairwFuncDataSize.end(),size_t(0));
    const size_t nTotResegmTemporalFuncDataSize = std::accumulate(anResegmTemporalFuncDataSize.begin(),anResegmTemporalFuncDataSize.end(),size_t(0));
    const size_t nTotResegmFuncDataSize = std::accumulate(anResegmFuncDataSize.begin(),anResegmFuncDataSize.end(),size_t(0));
    lvAssert(nTotResegmFuncDataSize==(nTotResegmUnaryFuncDataSize+nTotResegmPairwFuncDataSize+nTotResegmTemporalFuncDataSize));
    const size_t nModelSize = ((nStereoFuncDataSize+nTotResegmFuncDataSize)*sizeof(ValueType)/*+...externals unaccounted for, so x2*/*2);
    lvLog_(1,"Expecting total mem requirement <= %zu MB\n\t(~%zu MB for stereo graph, ~%zu MB for resegm graphs)",nModelSize/1024/1024,sizeof(ValueType)*nStereoFuncDataSize/1024/1024,sizeof(ValueType)*nTotResegmFuncDataSize/1024/1024);
    lvAssert__(nModelSize<(CACHE_MAX_SIZE_MB*1024*1024),"too many nodes/labels; model is unlikely to fit in memory (estimated: %zu MB)",nModelSize/1024/1024);
    lvLog(2,"Initializing graph lookup tables...");
    const size_t nCameraCount = getCameraCount();
    const size_t nLayerSize = m_oGridSize.total();
    lvDbgAssert(nCameraCount>size_t(0));
    lvDbgAssert(nLayerSize>size_t(0));
    const std::array<int,3> anAssocMapDims{int(m_oGridSize[0]),int((m_oGridSize[1]+m_nMaxDispOffset/*for oob usage*/)/m_nDispOffsetStep),int(m_nRealStereoLabels*m_nDispOffsetStep)};
    m_oAssocCounts.create(2,anAssocMapDims.data());
    m_oAssocMap.create(3,anAssocMapDims.data());
#if SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF
    m_oStereoUnaryCosts.create(m_oGridSize);
#elif SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
    m_oStereoUnaryCosts.create(int(m_nStereoLabels),int(anValidGraphNodes[m_nPrimaryCamIdx])); // flip for optim?
#endif //SEGMMATCH_CONFIG_USE_..._STEREO_INF
    m_oResegmUnaryCosts.create(int(m_oGridSize[0]*nTemporalLayerCount*nCameraCount),int(m_oGridSize[1]));
    m_vStereoGraphIdxToMapIdxLUT.reserve(anValidGraphNodes[m_nPrimaryCamIdx]);
    m_vResegmGraphIdxToMapIdxLUT.reserve(nTotValidNodes*nTemporalLayerCount);
    m_vStereoNodeMap.resize(nLayerSize);
    m_vResegmNodeMap.resize(nLayerSize*nCameraCount*nTemporalLayerCount);
    /// contains the (internal) labelings of the stereo/resegm graph (mutable for inference)
    m_oSuperStackedStereoLabeling.create(int(m_oGridSize[0]*nTemporalLayerCount*nCameraCount),int(m_oGridSize[1]));
    m_oSuperStackedResegmLabeling.create(int(m_oGridSize[0]*nTemporalLayerCount*nCameraCount),int(m_oGridSize[1]));
    m_oSuperStackedROI.create(int(m_oGridSize[0]*nTemporalLayerCount*nCameraCount),int(m_oGridSize[1]));
    m_oSuperStackedStereoLabeling = m_nDontCareLabelIdx;
    m_oSuperStackedResegmLabeling = s_nBackgroundLabelIdx;
    m_oSuperStackedROI = 0u;
    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
        m_aStackedInputMasks[nCamIdx].create(int(m_oGridSize[0]*nTemporalLayerCount),int(m_oGridSize[1]),CV_8UC1);
        m_aStackedInputMasks[nCamIdx] = s_nBackgroundLabelIdx;
        m_aStackedStereoLabelings[nCamIdx] = cv::Mat_<InternalLabelType>(int(m_oGridSize[0]*nTemporalLayerCount),int(m_oGridSize[1]),((InternalLabelType*)m_oSuperStackedStereoLabeling.data)+nLayerSize*nTemporalLayerCount*nCamIdx);
        m_aStackedResegmLabelings[nCamIdx] = cv::Mat_<InternalLabelType>(int(m_oGridSize[0]*nTemporalLayerCount),int(m_oGridSize[1]),((InternalLabelType*)m_oSuperStackedResegmLabeling.data)+nLayerSize*nTemporalLayerCount*nCamIdx);
        m_aStackedROIs[nCamIdx] = cv::Mat_<uchar>(int(m_oGridSize[0]*nTemporalLayerCount),int(m_oGridSize[1]),((uchar*)m_oSuperStackedROI.data)+nLayerSize*nTemporalLayerCount*nCamIdx);
        m_aGMMCompAssignMap[nCamIdx].create(int(m_oGridSize[0]*nTemporalLayerCount),int(m_oGridSize[1]));
        m_aShapeMaps[nCamIdx].create(m_oGridSize);
        m_aOcclusionMaps[nCamIdx].create(m_oGridSize);
        for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx) {
            m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Mask] = cv::Mat(m_oGridSize,CV_8UC1,m_aStackedInputMasks[nCamIdx].data+nLayerSize*nLayerIdx);
            m_aaStereoLabelings[nLayerIdx][nCamIdx] = cv::Mat_<InternalLabelType>(nRows,nCols,((InternalLabelType*)m_aStackedStereoLabelings[nCamIdx].data)+nLayerSize*nLayerIdx);
            m_aaResegmLabelings[nLayerIdx][nCamIdx] = cv::Mat_<InternalLabelType>(nRows,nCols,((InternalLabelType*)m_aStackedResegmLabelings[nCamIdx].data)+nLayerSize*nLayerIdx);
            m_aROIs[nCamIdx].copyTo(cv::Mat_<uchar>(nRows,nCols,m_aStackedROIs[nCamIdx].data+nLayerSize*nLayerIdx));
        }
    }
    m_nValidStereoGraphNodes = m_nValidResegmGraphNodes = size_t(0);
    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
        const size_t nCamIdxOffset = nTemporalLayerCount*nLayerSize*nCamIdx;
        for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx) {
            const size_t nLayerIdxOffset = nLayerIdx*nLayerSize;
            for(int nRowIdx=0; nRowIdx<nRows; ++nRowIdx) {
                for(int nColIdx=0; nColIdx<nCols; ++nColIdx) {
                    // could count cliques w/ stride here to make sure alloc below is perfect size...
                    const size_t nMapIdx = size_t(nRowIdx*nCols+nColIdx);
                    lvDbgAssert(nMapIdx<m_vStereoNodeMap.size());
                    const bool bValidGraphNode = m_aROIs[nCamIdx](nRowIdx,nColIdx)>0;
                    const bool bNearBorders = m_aDescROIs[nCamIdx](nRowIdx,nColIdx)==0;
                    if(nLayerIdx==0u && nCamIdx==m_nPrimaryCamIdx) {
                        StereoNodeInfo& oStereoNode = m_vStereoNodeMap[nMapIdx];
                        oStereoNode.nRowIdx = nRowIdx;
                        oStereoNode.nColIdx = nColIdx;
                        oStereoNode.bValidGraphNode = m_aROIs[m_nPrimaryCamIdx](nRowIdx,nColIdx)>0;
                        oStereoNode.bNearBorders = m_aDescROIs[m_nPrimaryCamIdx](nRowIdx,nColIdx)==0;
                        oStereoNode.nGraphNodeIdx = SIZE_MAX;
                        oStereoNode.nMapIdx = nMapIdx;
                        oStereoNode.nLUTIdx = nMapIdx; // full map LUT size is the actual map size in stereo graph
                        oStereoNode.nCamIdx = m_nPrimaryCamIdx; // always the same for stereo graph
                        oStereoNode.nLayerIdx = 0; // stereo graph also only has one layer
                        oStereoNode.nUnaryFactID = SIZE_MAX;
                        oStereoNode.pUnaryFunc = nullptr;
                        std::fill_n(oStereoNode.afPairwWeights.begin(),s_nPairwOrients,0.0f);
                        if(oStereoNode.bValidGraphNode) {
                            oStereoNode.nGraphNodeIdx = m_nValidStereoGraphNodes++;
                            m_vStereoGraphIdxToMapIdxLUT.push_back(nMapIdx);
                        }
                    }
                    const size_t nStackedMapIdx = nMapIdx+nLayerIdxOffset;
                    const size_t nLUTIdx = nStackedMapIdx+nCamIdxOffset;
                    lvDbgAssert(nLUTIdx<m_vResegmNodeMap.size());
                    ResegmNodeInfo& oResegmNode = m_vResegmNodeMap[nLUTIdx];
                    oResegmNode.nRowIdx = nRowIdx;
                    oResegmNode.nColIdx = nColIdx;
                    oResegmNode.bValidGraphNode = bValidGraphNode;
                    oResegmNode.bNearBorders = bNearBorders;
                    oResegmNode.nGraphNodeIdx = SIZE_MAX;
                    oResegmNode.nMapIdx = nMapIdx;
                    oResegmNode.nLUTIdx = nLUTIdx;
                    oResegmNode.nCamIdx = nCamIdx;
                    oResegmNode.nLayerIdx = nLayerIdx;
                    oResegmNode.nUnaryFactID = SIZE_MAX;
                    oResegmNode.pUnaryFunc = nullptr;
                    oResegmNode.nStackedIdx = nStackedMapIdx;
                    std::fill_n(oResegmNode.afPairwWeights.begin(),s_nPairwOrients,0.0f);
                    if(oResegmNode.bValidGraphNode) {
                        oResegmNode.nGraphNodeIdx = m_nValidResegmGraphNodes++;
                        m_vResegmGraphIdxToMapIdxLUT.push_back(nLUTIdx);
                    }
                }
            }
        }
    }
    lvAssert(m_nValidStereoGraphNodes==anValidGraphNodes[m_nPrimaryCamIdx]);
    lvAssert(m_nValidResegmGraphNodes==nTotValidNodes*nTemporalLayerCount);
    lvAssert(m_vStereoGraphIdxToMapIdxLUT.size()==anValidGraphNodes[m_nPrimaryCamIdx]);
    m_aStereoFuncsData = std::make_unique<ValueType[]>(nStereoFuncDataSize);
    m_pStereoUnaryFuncsDataBase = m_aStereoFuncsData.get();
    m_pStereoPairwFuncsDataBase = m_pStereoUnaryFuncsDataBase+nStereoUnaryFuncDataSize;
    m_pStereoEpipolarFuncsDataBase = m_pStereoPairwFuncsDataBase+nStereoPairwFuncDataSize;
    m_pStereoFuncsDataEnd = m_pStereoEpipolarFuncsDataBase+nStereoEpipolarFuncDataSize;
    m_aResegmFuncsData = std::make_unique<ValueType[]>(nTotResegmFuncDataSize);
    m_pResegmUnaryFuncsDataBase = m_aResegmFuncsData.get();
    m_pResegmPairwFuncsDataBase = m_pResegmUnaryFuncsDataBase+nTotResegmUnaryFuncDataSize;
    m_pResegmTemporalFuncsDataBase = m_pResegmPairwFuncsDataBase+nTotResegmPairwFuncDataSize;
    m_pResegmFuncsDataEnd = m_pResegmTemporalFuncsDataBase+nTotResegmTemporalFuncDataSize;
    for(size_t nLayerIdx=0; nLayerIdx<getTemporalLayerCount(); ++nLayerIdx)
        m_avFeatures[nLayerIdx].resize(FeatPackSize);
    lv::StopWatch oLocalTimer;
    lvLog(2,"Building stereo graph model...");
    buildStereoModel();
    lvLog(2,"Building resegm graph model...");
    buildResegmModel();
    lvLog_(2,"Graph models built in %f second(s).\n",oLocalTimer.tock());
}

void SegmMatcher::GraphModelData::buildStereoModel() {
    lvDbgExceptionWatch;
    lvLog(2,"\tadding base functions to stereo graph...");
    // reserves on graph created below need to be accurate (or larger than needed), otherwise function vectors will be reallocated, and pointers will be bad
    const size_t nStereoMaxMultiFactorsPerNode = /*pairw*/s_nPairwOrients + /*ho*/(SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN?size_t(1):size_t(0)); // epipolar stride not taken into account here
    m_pStereoModel = std::make_unique<StereoModelType>(StereoSpaceType(m_nValidStereoGraphNodes,(InternalLabelType)m_nStereoLabels),nStereoMaxMultiFactorsPerNode+1u);
    m_pStereoModel->reserveFunctions<ExplicitFunction>(m_nValidStereoGraphNodes); // unary factors only
    m_pStereoModel->reserveFunctions<ExplicitScaledFunction>(m_nValidStereoGraphNodes*nStereoMaxMultiFactorsPerNode);
    m_pStereoModel->reserveFunctions<ExplicitAllocFunction>(size_t(1)); // for pairw func base
    const std::vector<size_t> aPairwStereoFuncDims(s_nPairwOrients,m_nStereoLabels);
    m_oStereoPairwFuncID_base = m_pStereoModel->addFunction(ExplicitAllocFunction(aPairwStereoFuncDims.begin(),aPairwStereoFuncDims.end()));
    ExplicitAllocFunction& oStereoBaseFunc = m_pStereoModel->getFunction<ExplicitAllocFunction>(m_oStereoPairwFuncID_base);
    lvDbgAssert(oStereoBaseFunc.size()==m_nStereoLabels*m_nStereoLabels);
    for(InternalLabelType nLabelIdx1=0; nLabelIdx1<m_nRealStereoLabels; ++nLabelIdx1) {
        for(InternalLabelType nLabelIdx2=0; nLabelIdx2<m_nRealStereoLabels; ++nLabelIdx2) {
            const OutputLabelType nRealLabel1 = getRealLabel(nLabelIdx1);
            const OutputLabelType nRealLabel2 = getRealLabel(nLabelIdx2);
            const int nRealLabelDiff = std::min(std::abs((int)nRealLabel1-(int)nRealLabel2),SEGMMATCH_LBLSIM_STEREO_MAXDIFF_CST);
        #if SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST
            oStereoBaseFunc(nLabelIdx1,nLabelIdx2) = cost_cast(nRealLabelDiff*nRealLabelDiff);
        #else //!SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST
            oStereoBaseFunc(nLabelIdx1,nLabelIdx2) = cost_cast(nRealLabelDiff);
        #endif //!SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST
        }
    }
    for(size_t nLabelIdx=0; nLabelIdx<m_nRealStereoLabels; ++nLabelIdx) {
        oStereoBaseFunc(m_nDontCareLabelIdx,nLabelIdx) = cost_cast(10000);
        oStereoBaseFunc(nLabelIdx,m_nDontCareLabelIdx) = cost_cast(10000);
        oStereoBaseFunc(m_nOccludedLabelIdx,nLabelIdx) = cost_cast(0);
        oStereoBaseFunc(nLabelIdx,m_nOccludedLabelIdx) = cost_cast(0);
    }
    oStereoBaseFunc(m_nDontCareLabelIdx,m_nDontCareLabelIdx) = cost_cast(0);
    oStereoBaseFunc(m_nOccludedLabelIdx,m_nOccludedLabelIdx) = cost_cast(0);
    lvLog(2,"\tadding unary factors to stereo graph...");
    m_nStereoUnaryFactCount = size_t(0);
    const std::array<size_t,1> aUnaryStereoFuncDims = {m_nStereoLabels};
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode && oNode.nGraphNodeIdx==nGraphNodeIdx);
        oNode.vpCliques.clear();
        oNode.vCliqueMemberLUT.clear();
        FuncPairType oStereoFunc = m_pStereoModel->addFunctionWithRefReturn(ExplicitFunction());
        lvDbgAssert((&m_pStereoModel->getFunction<ExplicitFunction>(oStereoFunc.first))==(&oStereoFunc.second));
        oStereoFunc.second.assign(aUnaryStereoFuncDims.begin(),aUnaryStereoFuncDims.end(),m_pStereoUnaryFuncsDataBase+(nGraphNodeIdx*m_nStereoLabels));
        lvDbgAssert(&oStereoFunc.second(0)<m_pStereoPairwFuncsDataBase);
        lvDbgAssert(oStereoFunc.second.strides(0)==1); // expect no padding
        const std::array<size_t,1> aGraphNodeIndices = {nGraphNodeIdx};
        oNode.nUnaryFactID = m_pStereoModel->addFactorNonFinalized(oStereoFunc.first,aGraphNodeIndices.begin(),aGraphNodeIndices.end());
        lvDbgAssert(FuncIdentifType((*m_pStereoModel)[oNode.nUnaryFactID].functionIndex(),(*m_pStereoModel)[oNode.nUnaryFactID].functionType())==oStereoFunc.first);
        lvDbgAssert(m_pStereoModel->operator[](oNode.nUnaryFactID).numberOfVariables()==size_t(1));
        lvDbgAssert(oNode.nUnaryFactID==m_nStereoUnaryFactCount);
        oNode.pUnaryFunc = &oStereoFunc.second;
        ++m_nStereoUnaryFactCount;
    }
    lvLog(2,"\tadding pairwise factors to stereo graph...");
    m_nStereoCliqueCount = m_nStereoPairwFactCount = size_t(0);
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nBaseLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        StereoNodeInfo& oBaseNode = m_vStereoNodeMap[nBaseLUTNodeIdx];
        lvDbgAssert(oBaseNode.bValidGraphNode);
        oBaseNode.aPairwCliques = {}; // reset to default state
        const auto lPairwCliqueCreator = [&](size_t nOrientIdx, size_t nOffsetLUTNodeIdx) {
            StereoNodeInfo& oOffsetNode = m_vStereoNodeMap[nOffsetLUTNodeIdx];
            if(oOffsetNode.bValidGraphNode) {
                ScaledFuncPairType oStereoFunc = m_pStereoModel->addFunctionWithRefReturn(ExplicitScaledFunction());
                //oStereoFunc.second.assign(aPairwStereoFuncDims.begin(),aPairwStereoFuncDims.end(),m_pStereoPairwFuncsDataBase+((nGraphNodeIdx*s_nPairwOrients+nOrientIdx)*m_nStereoLabels*m_nStereoLabels));
                //lvDbgAssert(&oStereoFunc.second(0,0)<m_pStereoEpipolarFuncsDataBase);
                oStereoFunc.second.assign(aPairwStereoFuncDims.begin(),aPairwStereoFuncDims.end(),&oStereoBaseFunc.operator()(size_t(0)));
                lvDbgAssert(oStereoFunc.second.strides(0)==1 && oStereoFunc.second.strides(1)==m_nStereoLabels); // expect last-idx-major
                lvDbgAssert((&m_pStereoModel->getFunction<ExplicitScaledFunction>(oStereoFunc.first))==(&oStereoFunc.second));
                PairwClique& oPairwClique = oBaseNode.aPairwCliques[nOrientIdx];
                oPairwClique.m_bValid = true;
                const std::array<size_t,2> aLUTNodeIndices = {nBaseLUTNodeIdx,nOffsetLUTNodeIdx};
                oPairwClique.m_anLUTNodeIdxs = aLUTNodeIndices;
                const std::array<size_t,2> aGraphNodeIndices = {nGraphNodeIdx,oOffsetNode.nGraphNodeIdx};
                oPairwClique.m_anGraphNodeIdxs = aGraphNodeIndices;
                oPairwClique.m_nGraphFactorId = m_pStereoModel->addFactorNonFinalized(oStereoFunc.first,aGraphNodeIndices.begin(),aGraphNodeIndices.end());
                lvDbgAssert(FuncIdentifType((*m_pStereoModel)[oPairwClique.m_nGraphFactorId].functionIndex(),(*m_pStereoModel)[oPairwClique.m_nGraphFactorId].functionType())==oStereoFunc.first);
                lvDbgAssert(m_pStereoModel->operator[](oPairwClique.m_nGraphFactorId).numberOfVariables()==size_t(2));
                lvDbgAssert(oPairwClique.m_nGraphFactorId==m_nStereoUnaryFactCount+m_nStereoPairwFactCount);
                oPairwClique.m_pGraphFunctionPtr = &oStereoFunc.second;
                oBaseNode.vpCliques.push_back(&oPairwClique);
                oBaseNode.vCliqueMemberLUT.push_back(std::make_pair(m_nStereoCliqueCount,size_t(0)));
                oOffsetNode.vCliqueMemberLUT.push_back(std::make_pair(m_nStereoCliqueCount,size_t(1)));
                ++m_nStereoPairwFactCount;
                ++m_nStereoCliqueCount;
            }
        };
        if((oBaseNode.nRowIdx+1)<(int)m_oGridSize[0]) { // vertical pair
            lvDbgAssert(int((oBaseNode.nRowIdx+1)*m_oGridSize[1]+oBaseNode.nColIdx)==int(nBaseLUTNodeIdx+m_oGridSize[1]));
            lPairwCliqueCreator(size_t(0),nBaseLUTNodeIdx+m_oGridSize[1]);
        }
        if((oBaseNode.nColIdx+1)<(int)m_oGridSize[1]) { // horizontal pair
            lvDbgAssert(int(oBaseNode.nRowIdx*m_oGridSize[1]+oBaseNode.nColIdx+1)==int(nBaseLUTNodeIdx+1));
            lPairwCliqueCreator(size_t(1),nBaseLUTNodeIdx+1);
        }
        static_assert(s_nPairwOrients==2,"missing some pairw instantiations here");
    }
    m_nStereoEpipolarFactCount = size_t(0);
    if(SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN) {
        lvLog(2,"\tadding epipolar factors to stereo graph...");
        //    oBaseNode.oEpipolarClique = {};
        //    ... (check stride too)
        //    oBaseNode.vpCliques.push_back(&oBaseNode.oEpipolarClique);
        //    oBaseNode.vCliqueMemberLUT.push_back(std::make_pair(m_nStereoCliqueCount,size_t(0)));
        //        oOffsetNode.vCliqueMemberLUT.push_back(std::make_pair(m_nStereoCliqueCount,size_t(..)));
    #if SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN
        static_assert(false,"missing impl");
    #endif //SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN
        lvAssert(false); // missing impl
    }
    m_pStereoModel->finalize();
    lvDbgAssert(m_nStereoCliqueCount==(m_nStereoPairwFactCount+m_nStereoEpipolarFactCount));
    m_pStereoInf = std::make_unique<StereoGraphInference>(*this);
    if(lv::getVerbosity()>=2)
        lv::gm::printModelInfo(*m_pStereoModel);
}

void SegmMatcher::GraphModelData::updateStereoModel(bool bInit) {
    static_assert(getCameraCount()==2,"bad static array size, hardcoded stuff below (incl xor) will break");
    lvDbgExceptionWatch;
    lvDbgAssert_(m_nPrimaryCamIdx<getCameraCount(),"bad primary cam index");
    lvDbgAssert(m_pStereoModel && m_pStereoModel->numberOfVariables()==m_nValidStereoGraphNodes);
    lvDbgAssert(m_nValidStereoGraphNodes==m_vStereoGraphIdxToMapIdxLUT.size());
    const std::vector<cv::Mat>& vFeatures = m_avFeatures[0];
    lvDbgAssert(vFeatures.size()==FeatPackSize);
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    lvIgnore(nRows); lvIgnore(nCols);
    const cv::Mat_<float> oImgAffinity = vFeatures[FeatPack_ImgAffinity];
    const cv::Mat_<float> oShpAffinity = vFeatures[FeatPack_ShpAffinity];
    const cv::Mat_<float> oImgSaliency = vFeatures[FeatPack_ImgSaliency];
    const cv::Mat_<float> oShpSaliency = vFeatures[FeatPack_ShpSaliency];
    lvDbgAssert(oImgAffinity.dims==3 && oImgAffinity.size[0]==nRows && oImgAffinity.size[1]==nCols && oImgAffinity.size[2]==(int)m_nRealStereoLabels);
    lvDbgAssert(oShpAffinity.dims==3 && oShpAffinity.size[0]==nRows && oShpAffinity.size[1]==nCols && oShpAffinity.size[2]==(int)m_nRealStereoLabels);
    lvDbgAssert(oImgSaliency.dims==2 && oImgSaliency.size[0]==nRows && oImgSaliency.size[1]==nCols);
    lvDbgAssert(oShpSaliency.dims==2 && oShpSaliency.size[0]==nRows && oShpSaliency.size[1]==nCols);
    const cv::Mat_<uchar> oGradY = vFeatures[FeatPackOffset*m_nPrimaryCamIdx+FeatPackOffset_GradY];
    const cv::Mat_<uchar> oGradX = vFeatures[FeatPackOffset*m_nPrimaryCamIdx+FeatPackOffset_GradX];
    const cv::Mat_<uchar> oGradMag = vFeatures[FeatPackOffset*m_nPrimaryCamIdx+FeatPackOffset_GradMag];
    lvDbgAssert(m_oGridSize==oGradY.size && m_oGridSize==oGradX.size && m_oGridSize==oGradMag.size);
    /*const int nMinGradThrs = SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST-5;
    const int nMaxGradThrs = SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST+5;
    cv::imshow("oGradY",(oGradY>nMinGradThrs)&(oGradY<nMaxGradThrs));
    cv::imshow("oGradX",(oGradX>nMinGradThrs)&(oGradX<nMaxGradThrs));
    cv::waitKey(0);*/
    if(bInit) {
        for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
            lvDbgAssert(m_oGridSize==m_aShapeMaps[nCamIdx].size());
            lvDbgAssert(m_oGridSize==m_aOcclusionMaps[nCamIdx].size());
            m_aShapeMaps[nCamIdx] = 0;
            m_aOcclusionMaps[nCamIdx] = 0u;
            m_avMedianShapeLabels[nCamIdx].resize(1u,0u);
        }
    }
    lvLog(4,"Updating stereo graph model energy terms based on new features...");
    lv::StopWatch oLocalTimer;
#if SEGMMATCH_CONFIG_USE_PROGRESS_BARS
    lv::ProgressBarManager oProgressBarMgr("\tprogress:");
#endif //SEGMMATCH_CONFIG_USE_PROGRESS_BARS
#if USING_OPENMP
    #pragma omp parallel for
#endif //USING_OPENMP
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.nCamIdx==m_nPrimaryCamIdx);
        const int nRowIdx = oNode.nRowIdx;
        const int nColIdx = oNode.nColIdx;
    #if SEGMMATCH_CONFIG_USE_MEDIAN_DIST_COST
        const int nShapeIdx = m_aShapeMaps[m_nPrimaryCamIdx](nRowIdx,nColIdx);
        const InternalLabelType nMedianShapeLabel = m_avMedianShapeLabels[m_nPrimaryCamIdx][nShapeIdx];
    #endif //SEGMMATCH_CONFIG_USE_MEDIAN_DIST_COST
        // update unary terms for each grid node
        lvDbgAssert(nLUTNodeIdx==size_t(nRowIdx*nCols+nColIdx));
        lvDbgAssert(oNode.nUnaryFactID!=SIZE_MAX && oNode.nUnaryFactID<m_nStereoUnaryFactCount && oNode.pUnaryFunc);
        lvDbgAssert(m_pStereoModel->operator[](oNode.nUnaryFactID).numberOfVariables()==size_t(1));
        ExplicitFunction& vUnaryStereoLUT = *oNode.pUnaryFunc;
        lvDbgAssert(vUnaryStereoLUT.dimension()==1 && vUnaryStereoLUT.size()==m_nStereoLabels);
        lvDbgAssert__(oImgSaliency(nRowIdx,nColIdx)>=-1e-6f && oImgSaliency(nRowIdx,nColIdx)<=1.0f+1e-6f,"fImgSaliency = %1.10f @ [%d,%d]",oImgSaliency(nRowIdx,nColIdx),nRowIdx,nColIdx);
        lvDbgAssert__(oShpSaliency(nRowIdx,nColIdx)>=-1e-6f && oShpSaliency(nRowIdx,nColIdx)<=1.0f+1e-6f,"fShpSaliency = %1.10f @ [%d,%d]",oShpSaliency(nRowIdx,nColIdx),nRowIdx,nColIdx);
        const float fImgSaliency = std::max(oImgSaliency(nRowIdx,nColIdx),0.0f);
        const float fShpSaliency = std::max(oShpSaliency(nRowIdx,nColIdx),0.0f);
        ValueType tTotUnaryCost = cost_cast(0);
        int nValidUnaryCosts = 0;
        for(InternalLabelType nLabelIdx=0; nLabelIdx<m_nRealStereoLabels; ++nLabelIdx) {
            vUnaryStereoLUT(nLabelIdx) = cost_cast(0);
        #if SEGMMATCH_CONFIG_USE_MEDIAN_DIST_COST
            if(nShapeIdx!=0 && nMedianShapeLabel<m_nRealStereoLabels)
                vUnaryStereoLUT(nLabelIdx) += cost_cast(std::abs((int)nMedianShapeLabel-(int)nLabelIdx)*SEGMMATCH_LBLSIM_MEDIAN_DIST_SCALE_CST);
        #endif //SEGMMATCH_CONFIG_USE_MEDIAN_DIST_COST
            const int nOffsetColIdx = getOffsetColIdx(m_nPrimaryCamIdx,nColIdx,nLabelIdx);
            if(nOffsetColIdx>=0 && nOffsetColIdx<nCols && m_aROIs[m_nPrimaryCamIdx^1](nRowIdx,nOffsetColIdx)) {
                const float fImgAffinity = oImgAffinity(nRowIdx,nColIdx,nLabelIdx);
                const float fShpAffinity = oShpAffinity(nRowIdx,nColIdx,nLabelIdx);
                lvDbgAssert__(fImgAffinity>=0.0f,"fImgAffinity = %1.10f @ [%d,%d]",fImgAffinity,nRowIdx,nColIdx);
                lvDbgAssert__(fShpAffinity>=0.0f,"fShpAffinity = %1.10f @ [%d,%d]",fShpAffinity,nRowIdx,nColIdx);
                vUnaryStereoLUT(nLabelIdx) += cost_cast(fImgAffinity*fImgSaliency*SEGMMATCH_IMGSIM_COST_DESC_SCALE);
                vUnaryStereoLUT(nLabelIdx) += cost_cast(fShpAffinity*fShpSaliency*SEGMMATCH_SHPSIM_COST_DESC_SCALE);
            #if SEGMMATCH_CONFIG_USE_DISP_BG_HRST
                if(((InternalLabelType*)(m_aaResegmLabelings[oNode.nLayerIdx][m_nPrimaryCamIdx]).data)[oNode.nMapIdx]==s_nBackgroundLabelIdx)
                vUnaryStereoLUT(nLabelIdx) += cost_cast((float(nLabelIdx)/m_nRealStereoLabels)*100);
            #endif //SEGMMATCH_CONFIG_USE_DISP_BG_HRST
                tTotUnaryCost += vUnaryStereoLUT(nLabelIdx);
                ++nValidUnaryCosts;
            }
            else
                vUnaryStereoLUT(nLabelIdx) = cost_cast(tTotUnaryCost/(nValidUnaryCosts+1));
        }
        vUnaryStereoLUT(m_nDontCareLabelIdx) = cost_cast(10000);
    #if SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS
        vUnaryStereoLUT(m_nOccludedLabelIdx) = cost_cast((m_aOcclusionMaps[m_nPrimaryCamIdx].data[oNode.nMapIdx]>0u)?0:10000);
    #else //!SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS
        vUnaryStereoLUT(m_nOccludedLabelIdx) = cost_cast(10000);
    #endif //!SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS
        if(bInit) { // inter-spectral pairwise/epipolar term updates do not change w.r.t. segm or stereo updates
            for(size_t nOrientIdx=0; nOrientIdx<s_nPairwOrients; ++nOrientIdx) {
                PairwClique& oPairwClique = oNode.aPairwCliques[nOrientIdx];
                if(oPairwClique) {
                    lvDbgAssert(oPairwClique.m_nGraphFactorId>=m_nStereoUnaryFactCount);
                    lvDbgAssert(oPairwClique.m_nGraphFactorId<m_nStereoUnaryFactCount+m_nStereoPairwFactCount);
                    lvDbgAssert(m_pStereoModel->operator[](oPairwClique.m_nGraphFactorId).numberOfVariables()==size_t(2));
                    lvDbgAssert(oPairwClique.m_anGraphNodeIdxs[0]!=SIZE_MAX && oPairwClique.m_anLUTNodeIdxs[0]!=SIZE_MAX);
                    lvDbgAssert(oPairwClique.m_anGraphNodeIdxs[1]!=SIZE_MAX && oPairwClique.m_anLUTNodeIdxs[1]!=SIZE_MAX);
                    lvDbgAssert(m_vStereoNodeMap[oPairwClique.m_anLUTNodeIdxs[1]].bValidGraphNode && oPairwClique.m_pGraphFunctionPtr);
                    ExplicitScaledFunction& vPairwiseStereoFunc = *(ExplicitScaledFunction*)oPairwClique.m_pGraphFunctionPtr;
                    lvDbgAssert(vPairwiseStereoFunc.dimension()==2 && vPairwiseStereoFunc.size()==m_nStereoLabels*m_nStereoLabels);
                    const int nLocalGrad = (int)((nOrientIdx==0)?oGradY:(nOrientIdx==1)?oGradX:oGradMag)(nRowIdx,nColIdx);
                    const float fGradScaleFact = m_aLabelSimCostGradFactLUT.eval_raw(nLocalGrad);
                    const float fPairwWeight = (float)(fGradScaleFact*SEGMMATCH_LBLSIM_STEREO_SCALE_CST); // should be constant & uncapped for use in fastpd/bcd
                    // all stereo pairw functions are identical, but weighted differently (see base init in constructor)
                    oNode.afPairwWeights[nOrientIdx] = fPairwWeight;
                    vPairwiseStereoFunc.setScale(fPairwWeight);
                    // explicit debug check below VERY SLOW with large label space...
                    /*ExplicitAllocFunction& oStereoBaseFunc = m_pStereoModel->getFunction<ExplicitAllocFunction>(m_oStereoPairwFuncID_base);
                    for(InternalLabelType nLabelIdx1=0; nLabelIdx1<m_nRealStereoLabels; ++nLabelIdx1) {
                        for(InternalLabelType nLabelIdx2=0; nLabelIdx2<m_nRealStereoLabels; ++nLabelIdx2) {
                            const OutputLabelType nRealLabel1 = getRealLabel(nLabelIdx1);
                            const OutputLabelType nRealLabel2 = getRealLabel(nLabelIdx2);
                            const int nRealLabelDiff = std::min(std::abs((int)nRealLabel1-(int)nRealLabel2),SEGMMATCH_LBLSIM_STEREO_MAXDIFF_CST);
                        #if SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST
                            const ValueType tTestVal = cost_cast(nRealLabelDiff*nRealLabelDiff*fPairwWeight);
                        #else //!SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST
                            const ValueType tTestVal = cost_cast(nRealLabelDiff*fPairwWeight);
                        #endif //!SEGMMATCH_CONFIG_USE_SQR_LBL_DIFF_DIST
                            lvDbgAssert(tTestVal==cost_cast(oStereoBaseFunc(nLabelIdx1,nLabelIdx2)*fPairwWeight));
                            lvDbgAssert(tTestVal==vPairwiseStereoFunc(nLabelIdx1,nLabelIdx2));
                            lvDbgAssert(tTestVal==vPairwiseStereoFunc(std::vector<uchar>{nLabelIdx1,nLabelIdx2}.data()));

                        }
                    }*/
                }
            }
            lvAssert(!SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN); // add epipolar terms update here; missing impl
        }
    #if SEGMMATCH_CONFIG_USE_PROGRESS_BARS
        if(lv::getVerbosity()>=3)
            oProgressBarMgr.update(float(nGraphNodeIdx)/m_anValidGraphNodes[m_nPrimaryCamIdx]);
    #endif //SEGMMATCH_CONFIG_USE_PROGRESS_BARS
    }
    lvLog_(4,"Stereo graph model energy terms update completed in %f second(s).",oLocalTimer.tock());
}

void SegmMatcher::GraphModelData::resetStereoLabelings() {
    lvDbgExceptionWatch;
    lvDbgAssert_(m_pStereoModel,"model must be initialized first!");
    lvDbgAssert(m_nValidStereoGraphNodes==m_vStereoGraphIdxToMapIdxLUT.size());
    cv::Mat_<InternalLabelType>& oPrimaryLabeling = m_aaStereoLabelings[0][m_nPrimaryCamIdx];
    std::fill(oPrimaryLabeling.begin(),oPrimaryLabeling.end(),m_nDontCareLabelIdx);
#if SEGMMATCH_CONFIG_USE_LAST_STEREO_INIT
    if(m_nFramesProcessed>0u) {
        //cv::Mat oCurrLabelingDisplay = getStereoDispMapDisplay(1,m_nPrimaryCamIdx);
        //if(oCurrLabelingDisplay.size().area()<640*480)
        //    cv::resize(oCurrLabelingDisplay,oCurrLabelingDisplay,cv::Size(),2,2,cv::INTER_NEAREST);
        //cv::imshow(std::string("prewarp-")+std::to_string(m_nPrimaryCamIdx),oCurrLabelingDisplay);
        //cv::imshow("flow",lv::getFlowColorMap(m_avFeatures[0][FeatPackOffset*m_nPrimaryCamIdx+FeatPackOffset_OptFlow]));
        lv::remap_offset(m_aaStereoLabelings[1][m_nPrimaryCamIdx],oPrimaryLabeling,m_avFeatures[0][FeatPackOffset*m_nPrimaryCamIdx+FeatPackOffset_OptFlow],cv::INTER_NEAREST);
        oPrimaryLabeling.setTo(m_nDontCareLabelIdx,m_aROIs[m_nPrimaryCamIdx]==0u);
        lvDbgAssert(((InternalLabelType*)oPrimaryLabeling.data)+oPrimaryLabeling.total()==((InternalLabelType*)m_aaStereoLabelings[1][m_nPrimaryCamIdx].data));
        lvDbgAssert(cv::countNonZero(oPrimaryLabeling>=m_nStereoLabels)==0);
        //oCurrLabelingDisplay = getStereoDispMapDisplay(0,m_nPrimaryCamIdx);
        //if(oCurrLabelingDisplay.size().area()<640*480)
        //    cv::resize(oCurrLabelingDisplay,oCurrLabelingDisplay,cv::Size(),2,2,cv::INTER_NEAREST);
        //cv::imshow(std::string("postwarp-")+std::to_string(m_nPrimaryCamIdx),oCurrLabelingDisplay);
        //cv::waitKey(1);
        lvLog(4,"stereo-warp-init");
    }
#endif //SEGMMATCH_CONFIG_USE_LAST_STEREO_INIT
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode && oNode.nGraphNodeIdx==nGraphNodeIdx);
        lvDbgAssert(oNode.nUnaryFactID<m_pStereoModel->numberOfFactors());
        lvDbgAssert(m_pStereoModel->numberOfLabels(oNode.nUnaryFactID)==m_nStereoLabels);
        if(oPrimaryLabeling(oNode.nRowIdx,oNode.nColIdx)==m_nDontCareLabelIdx) {
            InternalLabelType nEvalLabel = oPrimaryLabeling(oNode.nRowIdx,oNode.nColIdx) = 0;
            const ExplicitFunction& vUnaryStereoLUT = *oNode.pUnaryFunc;
            ValueType fOptimalEnergy = vUnaryStereoLUT(nEvalLabel);
            for(nEvalLabel=1; nEvalLabel<m_nStereoLabels; ++nEvalLabel) {
                const ValueType fCurrEnergy = vUnaryStereoLUT(nEvalLabel);
                if(fOptimalEnergy>fCurrEnergy) {
                    oPrimaryLabeling(oNode.nRowIdx,oNode.nColIdx) = nEvalLabel;
                    fOptimalEnergy = fCurrEnergy;
                }
            }
        }
    }
    m_oAssocCounts = (AssocCountType)0;
    m_oAssocMap = (AssocIdxType)-1;
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        lvDbgAssert(nLUTNodeIdx==size_t(oNode.nRowIdx*(int)m_oGridSize(1)+oNode.nColIdx));
        const InternalLabelType nLabel = ((InternalLabelType*)oPrimaryLabeling.data)[nLUTNodeIdx];
        if(nLabel<m_nDontCareLabelIdx) // both special labels avoided here
            addAssoc(oNode.nRowIdx,oNode.nColIdx,nLabel);
    }
    resetStereoLabelingByProjection(m_nPrimaryCamIdx^1u);
}

void SegmMatcher::GraphModelData::resetStereoLabelingByProjection(size_t nCamIdx) {
    lvDbgExceptionWatch;
    lvDbgAssert_(nCamIdx<getCameraCount(),"bad input cam index");
    lvDbgAssert_(m_pStereoModel,"model must be initialized first!");
    lvDbgAssert(m_nValidStereoGraphNodes==m_vStereoGraphIdxToMapIdxLUT.size());
    cv::Mat_<InternalLabelType>& oLabeling = m_aaStereoLabelings[0][nCamIdx];
    lvDbgAssert(!oLabeling.empty() && m_oGridSize==oLabeling.size());
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    const size_t nBaseCamIdx = nCamIdx^1u;
    const cv::Mat_<InternalLabelType>& oBaseLabeling = m_aaStereoLabelings[0][nBaseCamIdx];
    std::fill(oLabeling.begin(),oLabeling.end(),m_nDontCareLabelIdx);
    const std::array<int,3> anVoteMapDims = {nRows,nCols,(int)m_nRealStereoLabels};
    m_oStereoVoteMap.create(3,anVoteMapDims.data());
    m_oStereoVoteMap = 0;
#if USING_OPENMP
    #pragma omp parallel for
#endif //USING_OPENMP
    for(int nRowIdx=0; nRowIdx<nRows; ++nRowIdx) {
        for(int nColIdx=0; nColIdx<nCols; ++nColIdx) {
            const InternalLabelType nCurrLabelIdx = oBaseLabeling(nRowIdx,nColIdx);
            lvDbgAssert((int)m_nRealStereoLabels==(int)m_nDontCareLabelIdx);
            if(nCurrLabelIdx<m_nRealStereoLabels) {
                const int nOffsetColIdx = getOffsetColIdx(nBaseCamIdx,nColIdx,nCurrLabelIdx);
                if(nOffsetColIdx>=0 && nOffsetColIdx<nCols && m_aROIs[nCamIdx](nRowIdx,nOffsetColIdx)) {
                    // reverse voting so that foreground has advantage on WTA
                    ++m_oStereoVoteMap(nRowIdx,nOffsetColIdx,(int)((m_nRealStereoLabels-1u)-nCurrLabelIdx));
                }
            }
        }
        for(int nColIdx=0; nColIdx<nCols; ++nColIdx) {
            if(m_aROIs[nCamIdx](nRowIdx,nColIdx)) {
                lvDbgAssert(size_t(ptrdiff_t(m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx+1)-m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx)))==m_nRealStereoLabels);
                const int* pWTAWinner = std::max_element(m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx),m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx+1));
                if(pWTAWinner!=m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx+1) && (*pWTAWinner)>0) {
                    const InternalLabelType nWinnerLabel = InternalLabelType((m_nRealStereoLabels-1u)-ptrdiff_t(pWTAWinner-m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx)));
                    lvDbgAssert(nWinnerLabel<m_nDontCareLabelIdx);
                    oLabeling(nRowIdx,nColIdx) = nWinnerLabel;
                }
                else
                    oLabeling(nRowIdx,nColIdx) = m_nOccludedLabelIdx;
            }
        }
    }
    cv::medianBlur(oLabeling,oLabeling,9);
    for(int nRowIdx=0; nRowIdx<nRows; ++nRowIdx) {
        for(int nColIdx=nCols-1; nColIdx>=0; --nColIdx) {
            InternalLabelType& nCurrLabel = oLabeling(nRowIdx,nColIdx);
            if(m_aROIs[nCamIdx](nRowIdx,nColIdx)==0u)
                nCurrLabel = m_nDontCareLabelIdx;
            else if(nCurrLabel==m_nDontCareLabelIdx) {
                for(int nOffset=0; nOffset<=(int)m_nMaxDispOffset; ++nOffset) {
                    const int nOffsetColIdx_pos = nColIdx+nOffset;
                    if(nOffsetColIdx_pos>=0 && nOffsetColIdx_pos<nCols && m_aROIs[nCamIdx](nRowIdx,nOffsetColIdx_pos)) {
                        const InternalLabelType& nNewLabel = oLabeling(nRowIdx,nOffsetColIdx_pos);
                        if(nNewLabel!=m_nDontCareLabelIdx) {
                            nCurrLabel = nNewLabel;
                            break;
                        }
                    }
                    const int nOffsetColIdx_neg = nColIdx-nOffset;
                    if(nOffsetColIdx_neg>=0 && nOffsetColIdx_neg<nCols && m_aROIs[nCamIdx](nRowIdx,nOffsetColIdx_neg)) {
                        const InternalLabelType& nNewLabel = oLabeling(nRowIdx,nOffsetColIdx_neg);
                        if(nNewLabel!=m_nDontCareLabelIdx) {
                            nCurrLabel = nNewLabel;
                            break;
                        }
                    }
                    if(nOffset==(int)m_nMaxDispOffset)
                    #if SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS
                        nCurrLabel = m_nOccludedLabelIdx;
                    #else //!SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS
                        nCurrLabel = InternalLabelType(0);
                    #endif //!SEGMMATCH_CONFIG_USE_OCCLUDED_LABELS
                }
            }
        }
    }
    lvDbgAssert(cv::countNonZero((oLabeling==m_nDontCareLabelIdx)&(m_aROIs[nCamIdx]))==0);
    if(lv::getVerbosity()>=3) {
        cv::Mat oCurrLabelingDisplay = getStereoDispMapDisplay(0,nCamIdx);
        if(oCurrLabelingDisplay.size().area()<640*480)
            cv::resize(oCurrLabelingDisplay,oCurrLabelingDisplay,cv::Size(),2,2,cv::INTER_NEAREST);
        cv::imshow(std::string("disp-")+std::to_string(nCamIdx),oCurrLabelingDisplay);
        cv::waitKey(1);
    }
}

void SegmMatcher::GraphModelData::computeMedianLabelings(size_t nTargetCamIdx) {
    lvDbgExceptionWatch;
    lvDbgAssert_(m_pStereoModel,"model must be initialized first!");
    lvDbgAssert(m_nValidStereoGraphNodes==m_vStereoGraphIdxToMapIdxLUT.size());
    const std::vector<size_t> vCamIdxs = (nTargetCamIdx<getCameraCount())?std::vector<size_t>{nTargetCamIdx}:std::vector<size_t>{m_nPrimaryCamIdx,m_nPrimaryCamIdx^1};
    for(size_t nCamIdx : vCamIdxs) {
        const int nShapeCount = cv::connectedComponents(m_aaResegmLabelings[0][nCamIdx],m_aShapeMaps[nCamIdx]);
        lvDbgAssert(nShapeCount>0); // background should always count as one component
        m_avMedianShapeLabels[nCamIdx].resize((size_t)nShapeCount);
        const cv::Mat_<InternalLabelType>& oLabeling = m_aaStereoLabelings[0][nCamIdx];
        for(int nShapeIdx=0; nShapeIdx<nShapeCount; ++nShapeIdx) {
            const int nMedianLabel = lv::calcMedianValue(oLabeling,m_aShapeMaps[nCamIdx]==nShapeIdx);
            lvDbgAssert(nMedianLabel>=0 && nMedianLabel<(int)m_nStereoLabels);
            m_avMedianShapeLabels[nCamIdx][nShapeIdx] = (InternalLabelType)nMedianLabel;
        }
    }
    if(lv::getVerbosity()>=4) {
        const float fRescaleFact = float(UCHAR_MAX)/(m_nMaxDispOffset-m_nMinDispOffset+1);
        for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
            cv::Mat oMedianStereoShapeDisp(m_oGridSize,CV_8UC3);
            for(int nRowIdx=0; nRowIdx<(int)m_oGridSize[0]; ++nRowIdx) {
                for(int nColIdx=0; nColIdx<(int)m_oGridSize[1]; ++nColIdx) {
                    const int nShapeIdx = m_aShapeMaps[nCamIdx](nRowIdx,nColIdx);
                    const OutputLabelType nRealMedianLabel = nShapeIdx==0?s_nDontCareLabel:getRealLabel(m_avMedianShapeLabels[nCamIdx][nShapeIdx]);
                    if(nRealMedianLabel==s_nDontCareLabel)
                        oMedianStereoShapeDisp.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(0,128,128);
                    else if(nRealMedianLabel==s_nOccludedLabel)
                        oMedianStereoShapeDisp.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(128,0,128);
                    else {
                        const uchar nIntensity = uchar((nRealMedianLabel-m_nMinDispOffset)*fRescaleFact);
                        oMedianStereoShapeDisp.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b::all(nIntensity);
                    }
                }
            }
            cv::imshow(std::string("oMedianStereoShapeDisp-")+std::to_string(nCamIdx),oMedianStereoShapeDisp);
        }
        cv::waitKey(1);
    }
}

void SegmMatcher::GraphModelData::computeOcclusionMaps(size_t nTargetCamIdx) {
    lvDbgExceptionWatch;
    lvDbgAssert_(m_pStereoModel,"model must be initialized first!");
    lvDbgAssert(m_nValidStereoGraphNodes==m_vStereoGraphIdxToMapIdxLUT.size());
    const std::vector<size_t> vCamIdxs = (nTargetCamIdx<getCameraCount())?std::vector<size_t>{nTargetCamIdx}:std::vector<size_t>{m_nPrimaryCamIdx,m_nPrimaryCamIdx^1};
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    const std::array<int,3> anVoteMapDims = {nRows,nCols,(int)m_nRealStereoLabels};
    m_oStereoVoteMap.create(3,anVoteMapDims.data());
    for(size_t nCamIdx : vCamIdxs) {
        const size_t nBaseCamIdx = nCamIdx^1;
        const cv::Mat_<InternalLabelType>& oBaseLabeling = m_aaStereoLabelings[0][nBaseCamIdx];
        m_oStereoVoteMap = 0;
        m_aOcclusionMaps[nCamIdx] = 0u;
    #if USING_OPENMP
        #pragma omp parallel for
    #endif //USING_OPENMP
        for(int nRowIdx=0; nRowIdx<nRows; ++nRowIdx) {
            for(int nColIdx=0; nColIdx<nCols; ++nColIdx) {
                const InternalLabelType nCurrLabelIdx = oBaseLabeling(nRowIdx,nColIdx);
                lvDbgAssert((int)m_nRealStereoLabels==(int)m_nDontCareLabelIdx);
                if(nCurrLabelIdx<m_nRealStereoLabels) {
                    const int nOffsetColIdx = getOffsetColIdx(nBaseCamIdx,nColIdx,nCurrLabelIdx);
                    if(nOffsetColIdx>=0 && nOffsetColIdx<nCols && m_aROIs[nCamIdx](nRowIdx,nOffsetColIdx)) {
                        // reverse voting so that foreground has advantage on WTA
                        ++m_oStereoVoteMap(nRowIdx,nOffsetColIdx,(int)((m_nRealStereoLabels-1u)-nCurrLabelIdx));
                    }
                }
            }
            for(int nColIdx=0; nColIdx<nCols; ++nColIdx) {
                if(m_aROIs[nCamIdx](nRowIdx,nColIdx)) {
                    lvDbgAssert(size_t(ptrdiff_t(m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx+1)-m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx)))==m_nRealStereoLabels);
                    const int* pWTAWinner = std::max_element(m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx),m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx+1));
                    if(pWTAWinner==m_oStereoVoteMap.ptr<int>(nRowIdx,nColIdx+1) || (*pWTAWinner)==0) {
                        m_aOcclusionMaps[nCamIdx](nRowIdx,nColIdx) = 255u;
                    }
                }
            }
        }
        cv::medianBlur(m_aOcclusionMaps[nCamIdx],m_aOcclusionMaps[nCamIdx],5);
        if(lv::getVerbosity()>=4) {
            cv::imshow(std::string("occlusion-map-")+std::to_string(nCamIdx),m_aOcclusionMaps[nCamIdx]);
            cv::waitKey(1);
        }
    }
}

void SegmMatcher::GraphModelData::buildResegmModel() {
    lvLog(2,"\tadding base functions to resegm graph...");
    // reserves on graph created below need to be accurate (or larger than needed), otherwise function vectors will be reallocated, and pointers will be bad
    const size_t nTemporalLayerCount = getTemporalLayerCount();
    const size_t nLayerSize = m_oGridSize.total();
    const size_t nResegmMaxFactorsPerNode = /*unary*/size_t(1) + /*pairw*/s_nPairwOrients + /*ho*/(SEGMMATCH_CONFIG_USE_TEMPORAL_CONN?size_t(1):size_t(0)); // temporal stride not taken into account here
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    lvIgnore(nRows); lvIgnore(nCols); lvIgnore(nLayerSize); lvIgnore(nTemporalLayerCount);
    const std::vector<size_t> aPairwResegmFuncDims(s_nPairwOrients,s_nResegmLabels);
    m_pResegmModel = std::make_unique<ResegmModelType>(ResegmSpaceType(m_nValidResegmGraphNodes),nResegmMaxFactorsPerNode);
    m_pResegmModel->reserveFunctions<ExplicitFunction>(m_nValidResegmGraphNodes*nResegmMaxFactorsPerNode);
    m_pResegmModel->reserveFunctions<ExplicitAllocFunction>(size_t(1)); // for pairw func base
    m_oResegmPairwFuncID_base = m_pResegmModel->addFunction(ExplicitAllocFunction(aPairwResegmFuncDims.begin(),aPairwResegmFuncDims.end()));
    ExplicitAllocFunction& oResegmBaseFunc = m_pResegmModel->getFunction<ExplicitAllocFunction>(m_oResegmPairwFuncID_base);
    lvDbgAssert(oResegmBaseFunc.size()==s_nResegmLabels*s_nResegmLabels);
    for(InternalLabelType nLabelIdx1=0; nLabelIdx1<s_nResegmLabels; ++nLabelIdx1)
        for(InternalLabelType nLabelIdx2=0; nLabelIdx2<s_nResegmLabels; ++nLabelIdx2)
            oResegmBaseFunc(nLabelIdx1,nLabelIdx2) = cost_cast((nLabelIdx1^nLabelIdx2)*SEGMMATCH_LBLSIM_RESEGM_SCALE_CST);
    lvLog(2,"\tadding unary factors to resegm graph...");
    m_nResegmUnaryFactCount = size_t(0);
    constexpr std::array<size_t,1> aUnaryResegmFuncDims = {s_nResegmLabels};
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
        ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
        oNode.vpCliques.clear();
        oNode.vCliqueMemberLUT.clear();
        lvDbgAssert(oNode.bValidGraphNode);
        FuncPairType oResegmFunc = m_pResegmModel->addFunctionWithRefReturn(ExplicitFunction());
        lvDbgAssert((&m_pResegmModel->getFunction<ExplicitFunction>(oResegmFunc.first))==(&oResegmFunc.second));
        oResegmFunc.second.assign(aUnaryResegmFuncDims.begin(),aUnaryResegmFuncDims.end(),m_pResegmUnaryFuncsDataBase+(nGraphNodeIdx*s_nResegmLabels));
        lvDbgAssert(&oResegmFunc.second(0)<m_pResegmPairwFuncsDataBase);
        lvDbgAssert(oResegmFunc.second.strides(0)==1); // expect no padding
        const std::array<size_t,1> aGraphNodeIndices = {nGraphNodeIdx};
        oNode.nUnaryFactID = m_pResegmModel->addFactorNonFinalized(oResegmFunc.first,aGraphNodeIndices.begin(),aGraphNodeIndices.end());
        lvDbgAssert(FuncIdentifType((*m_pResegmModel)[oNode.nUnaryFactID].functionIndex(),(*m_pResegmModel)[oNode.nUnaryFactID].functionType())==oResegmFunc.first);
        lvDbgAssert(m_pResegmModel->operator[](oNode.nUnaryFactID).numberOfVariables()==size_t(1));
        lvDbgAssert(oNode.nUnaryFactID==m_nResegmUnaryFactCount);
        oNode.pUnaryFunc = &oResegmFunc.second;
        ++m_nResegmUnaryFactCount;
    }
    lvLog(2,"\tadding pairwise factors to resegm graph...");
    m_nResegmCliqueCount = m_nResegmPairwFactCount = size_t(0);
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
        const size_t nBaseLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
        ResegmNodeInfo& oBaseNode = m_vResegmNodeMap[nBaseLUTNodeIdx];
        lvDbgAssert(oBaseNode.bValidGraphNode);
        oBaseNode.aPairwCliques = {}; // reset to default state
        const auto lPairwCliqueCreator = [&](size_t nOrientIdx, size_t nOffsetLUTNodeIdx) {
            ResegmNodeInfo& oOffsetNode = m_vResegmNodeMap[nOffsetLUTNodeIdx];
            if(oOffsetNode.bValidGraphNode) {
                FuncPairType oResegmFunc = m_pResegmModel->addFunctionWithRefReturn(ExplicitFunction());
                oResegmFunc.second.assign(aPairwResegmFuncDims.begin(),aPairwResegmFuncDims.end(),m_pResegmPairwFuncsDataBase+((nGraphNodeIdx*s_nPairwOrients+nOrientIdx)*s_nResegmLabels*s_nResegmLabels));
                lvDbgAssert(&oResegmFunc.second(0,0)<m_pResegmTemporalFuncsDataBase);
                lvDbgAssert(oResegmFunc.second.strides(0)==1 && oResegmFunc.second.strides(1)==s_nResegmLabels); // expect last-idx-major
                lvDbgAssert((&m_pResegmModel->getFunction<ExplicitFunction>(oResegmFunc.first))==(&oResegmFunc.second));
                PairwClique& oPairwClique = oBaseNode.aPairwCliques[nOrientIdx];
                oPairwClique.m_bValid = true;
                const std::array<size_t,2> aLUTNodeIndices = {nBaseLUTNodeIdx,nOffsetLUTNodeIdx};
                oPairwClique.m_anLUTNodeIdxs = aLUTNodeIndices;
                const std::array<size_t,2> aGraphNodeIndices = {nGraphNodeIdx,oOffsetNode.nGraphNodeIdx};
                oPairwClique.m_anGraphNodeIdxs = aGraphNodeIndices;
                oPairwClique.m_nGraphFactorId = m_pResegmModel->addFactorNonFinalized(oResegmFunc.first,aGraphNodeIndices.begin(),aGraphNodeIndices.end());
                lvDbgAssert(FuncIdentifType((*m_pResegmModel)[oPairwClique.m_nGraphFactorId].functionIndex(),(*m_pResegmModel)[oPairwClique.m_nGraphFactorId].functionType())==oResegmFunc.first);
                lvDbgAssert(m_pResegmModel->operator[](oPairwClique.m_nGraphFactorId).numberOfVariables()==size_t(2));
                lvDbgAssert(oPairwClique.m_nGraphFactorId==m_nResegmUnaryFactCount+m_nResegmPairwFactCount);
                oPairwClique.m_pGraphFunctionPtr = &oResegmFunc.second;
                oBaseNode.vpCliques.push_back(&oPairwClique);
                oBaseNode.vCliqueMemberLUT.push_back(std::make_pair(m_nResegmCliqueCount,size_t(0)));
                oOffsetNode.vCliqueMemberLUT.push_back(std::make_pair(m_nResegmCliqueCount,size_t(1)));
                ++m_nResegmPairwFactCount;
                ++m_nResegmCliqueCount;
            }
        };
        if((oBaseNode.nRowIdx+1)<nRows) { // vertical pair
            lvDbgAssert(int((oBaseNode.nRowIdx+1)*nCols+oBaseNode.nColIdx+(oBaseNode.nLayerIdx+oBaseNode.nCamIdx*nTemporalLayerCount)*nLayerSize)==int(nBaseLUTNodeIdx+nCols));
            lPairwCliqueCreator(size_t(0),nBaseLUTNodeIdx+nCols);
        }
        if((oBaseNode.nColIdx+1)<nCols) { // horizontal pair
            lvDbgAssert(int(oBaseNode.nRowIdx*nCols+oBaseNode.nColIdx+1+(oBaseNode.nLayerIdx+oBaseNode.nCamIdx*nTemporalLayerCount)*nLayerSize)==int(nBaseLUTNodeIdx+1));
            lPairwCliqueCreator(size_t(1),nBaseLUTNodeIdx+1);
        }
        static_assert(s_nPairwOrients==2,"missing some pairw instantiations here");
    }
    lvLog(2,"\tadding temporal factors to resegm graph...");
    m_nResegmTemporalFactCount = size_t(0);
#if SEGMMATCH_CONFIG_USE_TEMPORAL_CONN // needed here since struct members not instantiated for 0-sized cliques
    if(nTemporalLayerCount>1u) {
        const std::vector<size_t> aTemporalResegmFuncDims(nTemporalLayerCount,s_nResegmLabels);
        for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
            const size_t nBaseLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
            ResegmNodeInfo& oBaseNode = m_vResegmNodeMap[nBaseLUTNodeIdx];
            lvDbgAssert(oBaseNode.bValidGraphNode);
            oBaseNode.oTemporalClique = {};
            if(oBaseNode.nLayerIdx!=(nTemporalLayerCount-1u) || (oBaseNode.nRowIdx%s_nTemporalCliqueStride) || (oBaseNode.nColIdx%s_nTemporalCliqueStride))
                continue;
            std::vector<size_t> vnLUTNodeIdxs(1,nBaseLUTNodeIdx),vnGraphNodeIdxs(1,nGraphNodeIdx);
            lvDbgAssert(nBaseLUTNodeIdx>=(oBaseNode.nCamIdx*nTemporalLayerCount+nTemporalLayerCount-1u)*nLayerSize);
            lvDbgAssert(oBaseNode.nRowIdx*nCols+oBaseNode.nColIdx+int(((oBaseNode.nCamIdx+1u)*nTemporalLayerCount-1u)*nLayerSize)==int(nBaseLUTNodeIdx));
            for(size_t nOffsetLayerIdx=1; nOffsetLayerIdx<nTemporalLayerCount; ++nOffsetLayerIdx) {
                lvDbgAssert(nBaseLUTNodeIdx>=nOffsetLayerIdx*nLayerSize);
                const size_t nOffsetLUTNodeIdx = nBaseLUTNodeIdx-nOffsetLayerIdx*nLayerSize; // for initialization, just pick nodes with no displacement --- will update indices later
                lvDbgAssert(nOffsetLUTNodeIdx<m_vResegmNodeMap.size());
                const ResegmNodeInfo& oOffsetNode = m_vResegmNodeMap[nOffsetLUTNodeIdx];
                lvDbgAssert(oOffsetNode.bValidGraphNode && oOffsetNode.nCamIdx==oBaseNode.nCamIdx);
                lvDbgAssert(oOffsetNode.nLayerIdx==(oBaseNode.nLayerIdx-nOffsetLayerIdx));
                lvDbgAssert(oOffsetNode.nRowIdx==oBaseNode.nRowIdx && oOffsetNode.nColIdx==oBaseNode.nColIdx);
                vnLUTNodeIdxs.push_back(nOffsetLUTNodeIdx);
                vnGraphNodeIdxs.push_back(oOffsetNode.nGraphNodeIdx);
            }
            FuncPairType oResegmFunc = m_pResegmModel->addFunctionWithRefReturn(ExplicitFunction());
            const int nFuncSize = (int)std::pow((int)s_nResegmLabels,(int)s_nTemporalCliqueOrder);
            oResegmFunc.second.assign(aTemporalResegmFuncDims.begin(),aTemporalResegmFuncDims.end(),m_pResegmTemporalFuncsDataBase+(m_nResegmTemporalFactCount*nFuncSize)); // temporal stride not taken into account here (packed usage)
            lvDbgAssert(&oResegmFunc.second(0)<m_pResegmFuncsDataEnd);
            lvDbgAssert(oResegmFunc.second.strides(0)==1 && oResegmFunc.second.strides(1)==s_nResegmLabels); // expect last-idx-major
            lvDbgAssert((&m_pResegmModel->getFunction<ExplicitFunction>(oResegmFunc.first))==(&oResegmFunc.second));
            TemporalClique& oTemporalClique = oBaseNode.oTemporalClique;
            oTemporalClique.m_bValid = false; // will be reevaluated every time node displacements happen (i.e. in model update init call)
            std::reverse_copy(vnLUTNodeIdxs.begin(),vnLUTNodeIdxs.end(),oTemporalClique.m_anLUTNodeIdxs.begin());
            std::reverse_copy(vnGraphNodeIdxs.begin(),vnGraphNodeIdxs.end(),oTemporalClique.m_anGraphNodeIdxs.begin());
            oTemporalClique.m_nGraphFactorId = m_pResegmModel->addFactorNonFinalized(oResegmFunc.first,oTemporalClique.m_anGraphNodeIdxs.begin(),oTemporalClique.m_anGraphNodeIdxs.end());
            lvDbgAssert(FuncIdentifType((*m_pResegmModel)[oTemporalClique.m_nGraphFactorId].functionIndex(),(*m_pResegmModel)[oTemporalClique.m_nGraphFactorId].functionType())==oResegmFunc.first);
            lvDbgAssert(m_pResegmModel->operator[](oTemporalClique.m_nGraphFactorId).numberOfVariables()==nTemporalLayerCount);
            lvDbgAssert(oTemporalClique.m_nGraphFactorId==m_nResegmUnaryFactCount+m_nResegmPairwFactCount+m_nResegmTemporalFactCount);
            oTemporalClique.m_pGraphFunctionPtr = &oResegmFunc.second;
            ++m_nResegmTemporalFactCount;
            ++m_nResegmCliqueCount;
        }
    }
#endif //SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
    m_pResegmModel->finalize();
    lvDbgAssert(m_nResegmCliqueCount==(m_nResegmPairwFactCount+m_nResegmTemporalFactCount));
    m_pResegmInf = std::make_unique<ResegmGraphInference>(*this);
    if(lv::getVerbosity()>=2)
        lv::gm::printModelInfo(*m_pResegmModel);
}

void SegmMatcher::GraphModelData::updateResegmModel(bool bInit) {
    static_assert(getCameraCount()==2,"bad static array size, hardcoded stuff below (incl xor) will break");
    lvDbgExceptionWatch;
    const size_t nCameraCount = getCameraCount();
    const size_t nTemporalLayerCount = getTemporalLayerCount();
    const size_t nLayerSize = m_oGridSize.total();
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    lvIgnore(nRows); lvIgnore(nCols);
    lvDbgAssert(m_pResegmModel && m_pResegmModel->numberOfVariables()==m_nValidResegmGraphNodes);
    lvDbgAssert(m_nValidResegmGraphNodes==m_vResegmGraphIdxToMapIdxLUT.size());
    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
        lvDbgAssert(nCols==m_aGMMCompAssignMap[nCamIdx].cols);
        lvDbgAssert(nCols==m_aStackedResegmLabelings[nCamIdx].cols);
        lvDbgAssert(nRows*nTemporalLayerCount==size_t(m_aGMMCompAssignMap[nCamIdx].rows));
        lvDbgAssert(nRows*nTemporalLayerCount==size_t(m_aStackedResegmLabelings[nCamIdx].rows));
        lvDbgAssert(m_aGMMCompAssignMap[nCamIdx].size==m_aStackedInputImages[nCamIdx].size);
        lvDbgAssert(m_aStackedResegmLabelings[nCamIdx].size==m_aStackedInputImages[nCamIdx].size);
        for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx) {
            lvDbgAssert(m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Img].type()==m_aStackedInputImages[nCamIdx].type());
            lvDbgAssert(m_oGridSize==m_aaResegmLabelings[nLayerIdx][nCamIdx].size);
            lvDbgAssert(m_oGridSize==m_aaStereoLabelings[nLayerIdx][nCamIdx].size);
            lvDbgAssert(m_oGridSize==m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Img].size);
        }
    }
    const bool bDisplayDbgMaps = lv::getVerbosity()>=4;
#if SEGMMATCH_CONFIG_USE_GMM_LOCAL_BACKGR
    CamArray<cv::Mat_<uchar>> aGMMROIs = {(m_aStackedResegmLabelings[0]>0),(m_aStackedResegmLabelings[1]>0)};
    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
        for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx) {
            cv::Mat_<uchar> oCurrGMMROILayer(nRows,nCols,aGMMROIs[nCamIdx].data+nLayerSize*nLayerIdx);
        #if SEGMMATCH_DEFAULT_CONTOUR_DC_SIZE>0
            const int nDontCarePatchSize = SEGMMATCH_DEFAULT_CONTOUR_DC_SIZE;
            cv::Mat_<uchar> oInitSegmLayerDilated,oInitSegmLayerEroded;
            cv::dilate(oCurrGMMROILayer,oInitSegmLayerDilated,cv::getStructuringElement(cv::MORPH_RECT,cv::Size(nDontCarePatchSize,nDontCarePatchSize)));
            cv::erode(oCurrGMMROILayer,oInitSegmLayerEroded,cv::getStructuringElement(cv::MORPH_RECT,cv::Size(nDontCarePatchSize,nDontCarePatchSize)));
            cv::Mat_<uchar> oInitSegmContourMask = ~(oInitSegmLayerDilated^oInitSegmLayerEroded);
            //cv::imshow("oInitSegmContourMask",oInitSegmContourMask);
        #endif //SEGMMATCH_DEFAULT_CONTOUR_DC_SIZE>0
            const int nBGZonePatchSize = SEGMMATCH_DEFAULT_BG_ZONE_SIZE;
            cv::dilate(oCurrGMMROILayer,oCurrGMMROILayer,cv::getStructuringElement(cv::MORPH_RECT,cv::Size(nBGZonePatchSize,nBGZonePatchSize)));
        #if SEGMMATCH_DEFAULT_CONTOUR_DC_SIZE>0
            cv::bitwise_and(oCurrGMMROILayer,oInitSegmContourMask,oCurrGMMROILayer);
        #endif //SEGMMATCH_DEFAULT_CONTOUR_DC_SIZE>0
            cv::bitwise_and(oCurrGMMROILayer,m_aROIs[nCamIdx],oCurrGMMROILayer);
            lvDbgAssert(oCurrGMMROILayer.data==aGMMROIs[nCamIdx].data+nLayerSize*nLayerIdx); // ... should stay in-place
            //cv::imshow(std::string("oCurrGMMROILayer-")+std::to_string(nCamIdx),oCurrGMMROILayer);
            //cv::waitKey(1);
        }
    }
#else //!SEGMMATCH_CONFIG_USE_GMM_LOCAL_BACKGR
    CamArray<cv::Mat_<uchar>> aGMMROIs = {(m_aStackedROIs[0]>0),(m_aStackedROIs[1]>0)};
#endif //!SEGMMATCH_CONFIG_USE_GMM_LOCAL_BACKGR
    constexpr double dMinProbDensity = 1e-10;
    constexpr double dMaxProbDensity = 1.0;
    const double dLogProbFactor = -1./std::log(2.);
    CamArray<cv::Mat_<double>> aFGLogProb,aBGLogProb;
    CamArray<cv::Mat_<int>> aGMMClusterLabels;
#if USING_OPENMP
    #pragma omp parallel for num_threads(getCameraCount())
#endif //USING_OPENMP
    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
        const cv::Mat& oInputImage = m_aStackedInputImages[nCamIdx];
        const cv::Mat_<InternalLabelType>& oInputLabeling = m_aStackedResegmLabelings[nCamIdx];
        const cv::Mat_<uchar>& oROI = aGMMROIs[nCamIdx];
        cv::Mat_<int>& oAssignMap = m_aGMMCompAssignMap[nCamIdx];
        if(bInit)
            initGaussianMixtureParams(oInputImage,oInputLabeling,oROI,nCamIdx);
        assignGaussianMixtureComponents(oInputImage,oInputLabeling,oAssignMap,oROI,nCamIdx);
        learnGaussianMixtureParams(oInputImage,oInputLabeling,oAssignMap,oROI,nCamIdx);
        if(bDisplayDbgMaps) {
            aGMMClusterLabels[nCamIdx] = oAssignMap.clone();
            for(size_t nNodeIdx=0; nNodeIdx<oROI.total(); ++nNodeIdx) {
                if(((uint8_t*)oROI.data)[nNodeIdx]==0u)
                    ((int*)aGMMClusterLabels[nCamIdx].data)[nNodeIdx] = (1<<15)-1;
                else if(((InternalLabelType*)oInputLabeling.data)[nNodeIdx])
                    ((int*)aGMMClusterLabels[nCamIdx].data)[nNodeIdx] += 1<<15;
            }
        }
        aFGLogProb[nCamIdx].create(m_aStackedROIs[nCamIdx].size());
        aBGLogProb[nCamIdx].create(m_aStackedROIs[nCamIdx].size());
        aFGLogProb[nCamIdx] = 0.0;
        aBGLogProb[nCamIdx] = 0.0;
        for(size_t nElemIdx=0; nElemIdx<nLayerSize*nTemporalLayerCount; ++nElemIdx) {
            const double dColorFGProb = std::min(std::max(getGMMFGProb(oInputImage,nElemIdx,nCamIdx),dMinProbDensity),dMaxProbDensity);
            const double dColorBGProb = std::min(std::max(getGMMBGProb(oInputImage,nElemIdx,nCamIdx),dMinProbDensity),dMaxProbDensity);
            ((double*)aFGLogProb[nCamIdx].data)[nElemIdx] = dColorFGProb;
            ((double*)aBGLogProb[nCamIdx].data)[nElemIdx] = dColorBGProb;
        }
        cv::GaussianBlur(aFGLogProb[nCamIdx],aFGLogProb[nCamIdx],cv::Size(3,3),0);
        cv::GaussianBlur(aBGLogProb[nCamIdx],aBGLogProb[nCamIdx],cv::Size(3,3),0);
        cv::log(aFGLogProb[nCamIdx],aFGLogProb[nCamIdx]);
        cv::log(aBGLogProb[nCamIdx],aBGLogProb[nCamIdx]);
        aFGLogProb[nCamIdx] *= dLogProbFactor;
        aBGLogProb[nCamIdx] *= dLogProbFactor;
    }
    const float fInterSpectrScale = SEGMMATCH_SHPDIST_INTERSPEC_SCALE;
    TemporalArray<CamArray<cv::Mat_<float>>> aaFGDist,aaBGDist;
    TemporalArray<CamArray<cv::Mat_<uchar>>> aaGradY,aaGradX,aaGradMag;
    CamArray<TemporalArray<cv::Mat_<cv::Vec2f>>> aaOptFlow;
    CamArray<TemporalArray<cv::Mat_<uchar>>> aaTempDiff;
    for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx) {
        lvDbgAssert(m_avFeatures[nLayerIdx].size()==FeatPackSize);
        aaFGDist[nLayerIdx] = {m_avFeatures[nLayerIdx][FeatPack_LeftFGDist],m_avFeatures[nLayerIdx][FeatPack_RightFGDist]};
        aaBGDist[nLayerIdx] = {m_avFeatures[nLayerIdx][FeatPack_LeftBGDist],m_avFeatures[nLayerIdx][FeatPack_RightBGDist]};
        lvDbgAssert(lv::MatInfo(aaFGDist[nLayerIdx][0])==lv::MatInfo(aaFGDist[nLayerIdx][1]) && m_oGridSize==aaFGDist[nLayerIdx][0].size);
        lvDbgAssert(lv::MatInfo(aaBGDist[nLayerIdx][0])==lv::MatInfo(aaBGDist[nLayerIdx][1]) && m_oGridSize==aaBGDist[nLayerIdx][0].size);
        aaGradY[nLayerIdx] = {m_avFeatures[nLayerIdx][FeatPack_LeftGradY],m_avFeatures[nLayerIdx][FeatPack_RightGradY]};
        aaGradX[nLayerIdx] = {m_avFeatures[nLayerIdx][FeatPack_LeftGradX],m_avFeatures[nLayerIdx][FeatPack_RightGradX]};
        aaGradMag[nLayerIdx] = {m_avFeatures[nLayerIdx][FeatPack_LeftGradMag],m_avFeatures[nLayerIdx][FeatPack_RightGradMag]};
        lvDbgAssert(lv::MatInfo(aaGradY[nLayerIdx][0])==lv::MatInfo(aaGradY[nLayerIdx][1]) && m_oGridSize==aaGradY[nLayerIdx][0].size);
        lvDbgAssert(lv::MatInfo(aaGradX[nLayerIdx][0])==lv::MatInfo(aaGradX[nLayerIdx][1]) && m_oGridSize==aaGradX[nLayerIdx][0].size);
        lvDbgAssert(lv::MatInfo(aaGradMag[nLayerIdx][0])==lv::MatInfo(aaGradMag[nLayerIdx][1]) && m_oGridSize==aaGradMag[nLayerIdx][0].size);
        aaOptFlow[0][nLayerIdx] = m_avFeatures[nLayerIdx][FeatPack_LeftOptFlow];
        aaOptFlow[1][nLayerIdx] = m_avFeatures[nLayerIdx][FeatPack_RightOptFlow];
        lvDbgAssert(lv::MatInfo(aaOptFlow[0][nLayerIdx])==lv::MatInfo(aaOptFlow[1][nLayerIdx]) && m_oGridSize==aaOptFlow[0][nLayerIdx].size);
        aaTempDiff[0][nLayerIdx] = m_avFeatures[nLayerIdx][FeatPack_LeftTempDiff];
        aaTempDiff[1][nLayerIdx] = m_avFeatures[nLayerIdx][FeatPack_RightTempDiff];
        lvDbgAssert(lv::MatInfo(aaTempDiff[0][nLayerIdx])==lv::MatInfo(aaTempDiff[1][nLayerIdx]) && m_oGridSize==aaTempDiff[0][nLayerIdx].size);
    }
    lvLog(4,"Updating resegm graph model energy terms based on new features...");
    lv::StopWatch oLocalTimer;
    CamArray<cv::Mat_<double>> aFGLogProbMap,aBGLogProbMap;
    CamArray<cv::Mat_<double>> aFGDistMap,aBGDistMap;
    CamArray<cv::Mat_<cv::Vec3f>> aTempCoeffsDbgImgs;
    CamArray<cv::Mat> aTempLinkDbgImgs;
    if(bDisplayDbgMaps) {
        for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
            lvDbgAssert(m_aStackedResegmLabelings[nCamIdx].dims==2);
            aFGLogProbMap[nCamIdx].create(m_aStackedResegmLabelings[nCamIdx].size());
            aBGLogProbMap[nCamIdx].create(m_aStackedResegmLabelings[nCamIdx].size());
            aFGDistMap[nCamIdx].create(m_aStackedResegmLabelings[nCamIdx].size());
            aBGDistMap[nCamIdx].create(m_aStackedResegmLabelings[nCamIdx].size());
            aTempCoeffsDbgImgs[nCamIdx].create(m_aStackedResegmLabelings[nCamIdx].size());
            aFGLogProbMap[nCamIdx] = 0.0; aBGLogProbMap[nCamIdx] = 0.0;
            aFGDistMap[nCamIdx] = 0.0; aBGDistMap[nCamIdx] = 0.0;
            aTempCoeffsDbgImgs[nCamIdx] = cv::Vec3f(0,0,0);
            aTempLinkDbgImgs[nCamIdx] = m_aaInputs[0][nCamIdx*InputPackOffset+InputPackOffset_Img].clone();
            lvDbgAssert(aTempLinkDbgImgs[nCamIdx].channels()==1 || aTempLinkDbgImgs[nCamIdx].channels()==3);
            if(aTempLinkDbgImgs[nCamIdx].channels()==1)
                cv::cvtColor(aTempLinkDbgImgs[nCamIdx],aTempLinkDbgImgs[nCamIdx],cv::COLOR_GRAY2BGR);
        }
    }
#if USING_OPENMP
    #pragma omp parallel for
#endif //USING_OPENMP
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
        ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode);
        const size_t nCamIdx = oNode.nCamIdx;
        const size_t nLayerIdx = oNode.nLayerIdx;
        const size_t nMapIdx = oNode.nMapIdx;
        const size_t nStackedIdx = oNode.nStackedIdx;
        lvDbgAssert(nCamIdx<nCameraCount && nLayerIdx<nTemporalLayerCount && nMapIdx<nLayerSize && nStackedIdx<nLayerSize*nTemporalLayerCount);
        lvDbgAssert(nLUTNodeIdx==(nCamIdx*nTemporalLayerCount*nLayerSize+nLayerIdx*nLayerSize+nMapIdx) && nMapIdx==size_t(oNode.nRowIdx*nCols+oNode.nColIdx));
        // update unary terms for each grid node (will be readjusted in inter-spectral check next loop)
        lvDbgAssert(oNode.nUnaryFactID!=SIZE_MAX && oNode.nUnaryFactID<m_nResegmUnaryFactCount && oNode.pUnaryFunc);
        lvDbgAssert(m_pResegmModel->operator[](oNode.nUnaryFactID).numberOfVariables()==size_t(1));
        ExplicitFunction& vUnaryResegmLUT = *oNode.pUnaryFunc;
        lvDbgAssert(vUnaryResegmLUT.dimension()==1 && vUnaryResegmLUT.size()==s_nResegmLabels);
        const float fCurrFGDist = ((float*)aaFGDist[nLayerIdx][nCamIdx].data)[nMapIdx];
        const ValueType tFGDistUnaryCost = cost_cast(fCurrFGDist*SEGMMATCH_SHPDIST_COST_SCALE);
        const double dColorFGLogProb = ((double*)aFGLogProb[nCamIdx].data)[nStackedIdx];
        const ValueType tFGColorUnaryCost = cost_cast(dColorFGLogProb*SEGMMATCH_IMGSIM_COST_COLOR_SCALE);
        lvDbgAssert(tFGColorUnaryCost>=cost_cast(0) && tFGDistUnaryCost>=cost_cast(0));
        vUnaryResegmLUT(s_nForegroundLabelIdx) = tFGDistUnaryCost+tFGColorUnaryCost;
        const float fCurrBGDist = ((float*)aaBGDist[nLayerIdx][nCamIdx].data)[nMapIdx];
        const ValueType tBGDistUnaryCost = cost_cast(fCurrBGDist*SEGMMATCH_SHPDIST_COST_SCALE);
        const double dColorBGLogProb = ((double*)aBGLogProb[nCamIdx].data)[nStackedIdx];
        const ValueType tBGColorUnaryCost = cost_cast(dColorBGLogProb*SEGMMATCH_IMGSIM_COST_COLOR_SCALE);
        lvDbgAssert(tBGColorUnaryCost>=cost_cast(0) && tBGDistUnaryCost>=cost_cast(0));
        vUnaryResegmLUT(s_nBackgroundLabelIdx) = tBGDistUnaryCost+tBGColorUnaryCost;
        if(bInit) {
            // need to clear clique infos; will update temporal links in next loop
            oNode.vpCliques.clear();
            oNode.vCliqueMemberLUT.clear();
        }
        if(bDisplayDbgMaps) {
            ((double*)aFGLogProbMap[nCamIdx].data)[nStackedIdx] = dColorFGLogProb;
            ((double*)aBGLogProbMap[nCamIdx].data)[nStackedIdx] = dColorBGLogProb;
            ((double*)aFGDistMap[nCamIdx].data)[nStackedIdx] = fCurrFGDist;
            ((double*)aBGDistMap[nCamIdx].data)[nStackedIdx] = fCurrBGDist;
        }
    }
    std::atomic_size_t nCliqueIdx{0};
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
        ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode);
        const int nRowIdx = oNode.nRowIdx;
        const int nColIdx = oNode.nColIdx;
        const size_t nCamIdx = oNode.nCamIdx;
        const size_t nLayerIdx = oNode.nLayerIdx;
        lvDbgAssert(nCamIdx<nCameraCount && nLayerIdx<nTemporalLayerCount && oNode.nMapIdx<nLayerSize && oNode.nStackedIdx<nLayerSize*nTemporalLayerCount);
        lvDbgAssert(nLUTNodeIdx==(nCamIdx*nTemporalLayerCount*nLayerSize+nLayerIdx*nLayerSize+oNode.nMapIdx) && oNode.nMapIdx==size_t(nRowIdx*nCols+nColIdx));
        lvDbgAssert(oNode.nUnaryFactID!=SIZE_MAX && oNode.nUnaryFactID<m_nResegmUnaryFactCount && oNode.pUnaryFunc);
        lvDbgAssert(m_pResegmModel->operator[](oNode.nUnaryFactID).numberOfVariables()==size_t(1));
        ExplicitFunction& vUnaryResegmLUT = *oNode.pUnaryFunc;
        const InternalLabelType nStereoLabelIdx = ((InternalLabelType*)m_oSuperStackedStereoLabeling.data)[nLUTNodeIdx];
        const int nOffsetColIdx = (nStereoLabelIdx<m_nRealStereoLabels)?getOffsetColIdx(nCamIdx,nColIdx,nStereoLabelIdx):INT_MAX;
        if(nOffsetColIdx>=0 && nOffsetColIdx<nCols && m_aROIs[nCamIdx^1](nRowIdx,nOffsetColIdx)) {
            const float fCurrOffsetFGDist = aaFGDist[nLayerIdx][nCamIdx^1](nRowIdx,nOffsetColIdx);
            const ValueType tOffsetFGDistUnaryCost = cost_cast(fCurrOffsetFGDist*SEGMMATCH_SHPDIST_COST_SCALE*SEGMMATCH_SHPDIST_INTERSPEC_SCALE);
            vUnaryResegmLUT(s_nForegroundLabelIdx) += tOffsetFGDistUnaryCost;
            const float fCurrOffsetBGDist = aaBGDist[nLayerIdx][nCamIdx^1](nRowIdx,nOffsetColIdx);
            const ValueType tOffsetBGDistUnaryCost = cost_cast(fCurrOffsetBGDist*SEGMMATCH_SHPDIST_COST_SCALE*SEGMMATCH_SHPDIST_INTERSPEC_SCALE);
            vUnaryResegmLUT(s_nBackgroundLabelIdx) += tOffsetBGDistUnaryCost;
            for(size_t nOrientIdx=0; nOrientIdx<s_nPairwOrients; ++nOrientIdx) {
                PairwClique& oPairwClique = oNode.aPairwCliques[nOrientIdx];
                if(oPairwClique) {
                    lvDbgAssert(oPairwClique.m_nGraphFactorId>=m_nResegmUnaryFactCount && oPairwClique.m_nGraphFactorId<m_nResegmUnaryFactCount+m_nResegmPairwFactCount);
                    lvDbgAssert(m_pResegmModel->operator[](oPairwClique.m_nGraphFactorId).numberOfVariables()==size_t(2));
                    lvDbgAssert(oPairwClique.m_anGraphNodeIdxs[0]!=SIZE_MAX && oPairwClique.m_anLUTNodeIdxs[0]!=SIZE_MAX);
                    lvDbgAssert(oPairwClique.m_anGraphNodeIdxs[1]!=SIZE_MAX && oPairwClique.m_anLUTNodeIdxs[1]!=SIZE_MAX);
                    lvDbgAssert(m_vResegmNodeMap[oPairwClique.m_anLUTNodeIdxs[1]].bValidGraphNode && oPairwClique.m_pGraphFunctionPtr);
                    ExplicitFunction& vPairwResegmLUT = *(ExplicitFunction*)oPairwClique.m_pGraphFunctionPtr;
                    lvDbgAssert(vPairwResegmLUT.dimension()==2 && vPairwResegmLUT.size()==s_nResegmLabels*s_nResegmLabels);
                    const int nLocalGrad = (int)(((nOrientIdx==0)?aaGradY[nLayerIdx][nCamIdx]:(nOrientIdx==1)?aaGradX[nLayerIdx][nCamIdx]:aaGradMag[nLayerIdx][nCamIdx])(nRowIdx,nColIdx));
                    const int nOffsetGrad = (int)(((nOrientIdx==0)?aaGradY[nLayerIdx][nCamIdx^1]:(nOrientIdx==1)?aaGradX[nLayerIdx][nCamIdx^1]:aaGradMag[nLayerIdx][nCamIdx^1])(nRowIdx,nOffsetColIdx));
                    const float fLocalScaleFact = m_aLabelSimCostGradFactLUT.eval_raw(nLocalGrad);
                    const float fOffsetScaleFact = m_aLabelSimCostGradFactLUT.eval_raw(nOffsetGrad);
                    const float fScaleFact = std::min(fLocalScaleFact,fOffsetScaleFact/fInterSpectrScale); // simplified, and better w/ more layers
                    //const float fScaleFact = (fLocalScaleFact+fOffsetScaleFact*fInterSpectrScale)/(fInterSpectrScale+1.0f);
                    for(InternalLabelType nLabelIdx1=0; nLabelIdx1<s_nResegmLabels; ++nLabelIdx1) {
                        for(InternalLabelType nLabelIdx2=0; nLabelIdx2<s_nResegmLabels; ++nLabelIdx2) {
                            vPairwResegmLUT(nLabelIdx1,nLabelIdx2) = cost_cast((nLabelIdx1^nLabelIdx2)*fScaleFact*SEGMMATCH_LBLSIM_RESEGM_SCALE_CST);
                        }
                    }
                    if(bInit) {
                        oNode.vpCliques.push_back(&oPairwClique);
                    #if USING_OPENMP
                        #pragma omp critical
                    #endif //USING_OPENMP
                        {
                            const size_t nCurrCliqueIdx = nCliqueIdx++;
                            for(size_t nPairIdx=0; nPairIdx<2u; ++nPairIdx)
                                m_vResegmNodeMap[oPairwClique.m_anLUTNodeIdxs[nPairIdx]].vCliqueMemberLUT.push_back(std::make_pair(nCurrCliqueIdx,nPairIdx));
                        }
                    }
                }
            }
        }
        else {
            vUnaryResegmLUT(s_nForegroundLabelIdx) = cost_cast(100); // oob foreground neighb-copy cost
            vUnaryResegmLUT(s_nBackgroundLabelIdx) = cost_cast(0);
            for(size_t nOrientIdx=0; nOrientIdx<s_nPairwOrients; ++nOrientIdx) {
                PairwClique& oPairwClique = oNode.aPairwCliques[nOrientIdx];
                if(oPairwClique) {
                    lvDbgAssert(oPairwClique.m_nGraphFactorId>=m_nResegmUnaryFactCount && oPairwClique.m_nGraphFactorId<m_nResegmUnaryFactCount+m_nResegmPairwFactCount);
                    lvDbgAssert(m_pResegmModel->operator[](oPairwClique.m_nGraphFactorId).numberOfVariables()==size_t(2));
                    lvDbgAssert(oPairwClique.m_anGraphNodeIdxs[0]!=SIZE_MAX && oPairwClique.m_anLUTNodeIdxs[0]!=SIZE_MAX);
                    lvDbgAssert(oPairwClique.m_anGraphNodeIdxs[1]!=SIZE_MAX && oPairwClique.m_anLUTNodeIdxs[1]!=SIZE_MAX);
                    lvDbgAssert(m_vResegmNodeMap[oPairwClique.m_anLUTNodeIdxs[1]].bValidGraphNode && oPairwClique.m_pGraphFunctionPtr);
                    ExplicitFunction& vPairwResegmLUT = *(ExplicitFunction*)oPairwClique.m_pGraphFunctionPtr;
                    lvDbgAssert(vPairwResegmLUT.dimension()==2 && vPairwResegmLUT.size()==s_nResegmLabels*s_nResegmLabels);
                    const int nLocalGrad = (int)(((nOrientIdx==0)?aaGradY[nLayerIdx][nCamIdx]:(nOrientIdx==1)?aaGradX[nLayerIdx][nCamIdx]:aaGradMag[nLayerIdx][nCamIdx])(nRowIdx,nColIdx));
                    const float fLocalScaleFact = m_aLabelSimCostGradFactLUT.eval_raw(nLocalGrad);
                    for(InternalLabelType nLabelIdx1=0; nLabelIdx1<s_nResegmLabels; ++nLabelIdx1) {
                        for(InternalLabelType nLabelIdx2=0; nLabelIdx2<s_nResegmLabels; ++nLabelIdx2) {
                            vPairwResegmLUT(nLabelIdx1,nLabelIdx2) = cost_cast((nLabelIdx1^nLabelIdx2)*fLocalScaleFact*SEGMMATCH_LBLSIM_RESEGM_SCALE_CST);
                        }
                    }
                    if(bInit) {
                        oNode.vpCliques.push_back(&oPairwClique);
                    #if USING_OPENMP
                        #pragma omp critical
                    #endif //USING_OPENMP
                        {
                            const size_t nCurrCliqueIdx = nCliqueIdx++;
                            for(size_t nPairIdx=0; nPairIdx<2u; ++nPairIdx)
                                m_vResegmNodeMap[oPairwClique.m_anLUTNodeIdxs[nPairIdx]].vCliqueMemberLUT.push_back(std::make_pair(nCurrCliqueIdx,nPairIdx));
                        }
                    }
                }
            }
        }
    }
#if SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
    if(bInit && nTemporalLayerCount>1u && m_nFramesProcessed>=(nTemporalLayerCount-1u)) {
    #if USING_OPENMP
        #pragma omp parallel for
    #endif //USING_OPENMP
        for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
            const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
            ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
            lvDbgAssert(oNode.bValidGraphNode);
            const int nRowIdx = oNode.nRowIdx; lvIgnore(nRowIdx);
            const int nColIdx = oNode.nColIdx; lvIgnore(nColIdx);
            const size_t nCamIdx = oNode.nCamIdx; lvIgnore(nCamIdx);
            const size_t nLayerIdx = oNode.nLayerIdx; lvIgnore(nLayerIdx);
            lvDbgAssert(nCamIdx<nCameraCount && nLayerIdx<nTemporalLayerCount && oNode.nMapIdx<nLayerSize && oNode.nStackedIdx<nLayerSize*nTemporalLayerCount);
            lvDbgAssert(nLUTNodeIdx==(nCamIdx*nTemporalLayerCount*nLayerSize+nLayerIdx*nLayerSize+oNode.nMapIdx) && oNode.nMapIdx==size_t(nRowIdx*nCols+nColIdx));
            TemporalClique& oTemporalClique = oNode.oTemporalClique;
            if(oTemporalClique.m_nGraphFactorId!=SIZE_MAX) {
                lvDbgAssert(nLayerIdx==nTemporalLayerCount-1u && oTemporalClique.m_anGraphNodeIdxs.back()==nGraphNodeIdx);
                lvDbgAssert(oTemporalClique.m_pGraphFunctionPtr && m_pResegmModel->operator[](oTemporalClique.m_nGraphFactorId).numberOfVariables()==oTemporalClique.getSize());
                std::vector<size_t> vnMapIdxs(1,oNode.nMapIdx),vnLUTNodeIdxs(1,nLUTNodeIdx),vnGraphNodeIdxs(1,nGraphNodeIdx);
                lvDbgAssert(nLUTNodeIdx>=(nCamIdx*nTemporalLayerCount+nTemporalLayerCount-1)*nLayerSize);
                lvDbgAssert(nRowIdx*nCols+nColIdx+int(((nCamIdx+1)*nTemporalLayerCount-1)*nLayerSize)==int(nLUTNodeIdx));
                lvDbgAssert(s_nTemporalCliqueEdges==nTemporalLayerCount-1u);
                oTemporalClique.m_bValid = true; // temporary assignment, may be revoked by checks below
                const size_t nOldestLayer = nTemporalLayerCount-1u;
                for(size_t nOffsetLayerIdx=1; nOffsetLayerIdx<nTemporalLayerCount; ++nOffsetLayerIdx) {
                    const size_t nCurrLayer = nOldestLayer-nOffsetLayerIdx;
                    const size_t nPreviousLUTNodeIdx = vnLUTNodeIdxs.back();
                    lvDbgAssert(nPreviousLUTNodeIdx<nCameraCount*nTemporalLayerCount*nLayerSize);
                    const ResegmNodeInfo& oPreviousNode = m_vResegmNodeMap[nPreviousLUTNodeIdx];
                    const cv::Vec2f& vFlowDir = aaOptFlow[nCamIdx][nCurrLayer](oPreviousNode.nRowIdx,oPreviousNode.nColIdx);
                    const int nRowOffset = std::max(std::min((int)std::round((float)oPreviousNode.nRowIdx+vFlowDir[1]),nRows-1),0)-oPreviousNode.nRowIdx;
                    const int nColOffset = std::max(std::min((int)std::round((float)oPreviousNode.nColIdx+vFlowDir[0]),nCols-1),0)-oPreviousNode.nColIdx;
                    const size_t nCurrLUTNodeIdx = (nPreviousLUTNodeIdx+(nRowOffset*nCols+nColOffset))-nLayerSize;
                    lvDbgAssert(nCurrLUTNodeIdx<((nCamIdx*nTemporalLayerCount+(oPreviousNode.nLayerIdx+1u))*nLayerSize));
                    lvDbgAssert(nCurrLUTNodeIdx>=((nCamIdx*nTemporalLayerCount+(oPreviousNode.nLayerIdx-1u))*nLayerSize));
                    const ResegmNodeInfo& oCurrNode = m_vResegmNodeMap[nCurrLUTNodeIdx];
                    lvDbgAssert(oCurrNode.nCamIdx==nCamIdx && oCurrNode.nLayerIdx==nCurrLayer);
                    lvDbgAssert(oCurrNode.nRowIdx==(oPreviousNode.nRowIdx+nRowOffset) && oCurrNode.nColIdx==(oPreviousNode.nColIdx+nColOffset));
                    vnMapIdxs.push_back(oCurrNode.nMapIdx);
                    vnLUTNodeIdxs.push_back(nCurrLUTNodeIdx);
                    vnGraphNodeIdxs.push_back(oCurrNode.nGraphNodeIdx);
                    oTemporalClique.m_bValid &= oCurrNode.bValidGraphNode;
                    if(bDisplayDbgMaps) {
                        if(nCurrLayer==0u && oPreviousNode.bValidGraphNode && oCurrNode.bValidGraphNode) {
                            if((oPreviousNode.nRowIdx%16)==0 && (oPreviousNode.nColIdx%16)==0) { // to sparsify the optflow debug map a little bit
                                const cv::Point vInit(oPreviousNode.nColIdx,oPreviousNode.nRowIdx),vEnd(oCurrNode.nColIdx,oCurrNode.nRowIdx);
                                if(vInit!=vEnd) {
                                    cv::line(aTempLinkDbgImgs[oCurrNode.nCamIdx],vInit,vEnd,cv::Scalar_<uchar>(255,0,0),1);
                                    cv::circle(aTempLinkDbgImgs[oPreviousNode.nCamIdx],vInit,1,cv::Scalar_<uchar>(0,0,255),-1);
                                    cv::circle(aTempLinkDbgImgs[oCurrNode.nCamIdx],vEnd,1,cv::Scalar_<uchar>(0,255,0),-1);
                                }
                                else
                                    cv::circle(aTempLinkDbgImgs[oCurrNode.nCamIdx],vEnd,1,cv::Scalar_<uchar>(0,255,255),-1);
                            }
                        }
                    }
                }
                if(oTemporalClique.m_bValid) {
                    std::reverse(vnMapIdxs.begin(),vnMapIdxs.end());
                    std::reverse_copy(vnLUTNodeIdxs.begin(),vnLUTNodeIdxs.end(),oTemporalClique.m_anLUTNodeIdxs.begin());
                    std::reverse_copy(vnGraphNodeIdxs.begin(),vnGraphNodeIdxs.end(),oTemporalClique.m_anGraphNodeIdxs.begin());
                    m_pResegmModel->setFactorVariables(oTemporalClique.m_nGraphFactorId,oTemporalClique.m_anGraphNodeIdxs.begin(),oTemporalClique.m_anGraphNodeIdxs.end());
                    lvDbgAssert(m_pResegmModel->operator[](oTemporalClique.m_nGraphFactorId).numberOfVariables()==nTemporalLayerCount);
                    oNode.vpCliques.push_back(&oTemporalClique);
                #if USING_OPENMP
                    #pragma omp critical
                #endif //USING_OPENMP
                    {
                        const size_t nCurrCliqueIdx = nCliqueIdx++;
                        for(size_t nOffsetLayerIdx=0; nOffsetLayerIdx<nTemporalLayerCount; ++nOffsetLayerIdx)
                            m_vResegmNodeMap[vnLUTNodeIdxs[nOffsetLayerIdx]].vCliqueMemberLUT.push_back(std::make_pair(nCurrCliqueIdx,nOffsetLayerIdx));
                    }
                    ExplicitFunction& vTemporalResegmLUT = *(ExplicitFunction*)oTemporalClique.m_pGraphFunctionPtr;
                    lvDbgAssert(vTemporalResegmLUT.dimension()==oTemporalClique.getSize() && vTemporalResegmLUT.size()==std::pow(s_nResegmLabels,oTemporalClique.getSize()));
                    lvDbgAssert(&vTemporalResegmLUT(0)<m_pResegmFuncsDataEnd && vTemporalResegmLUT.strides(0)==1 && vTemporalResegmLUT.strides(1)==s_nResegmLabels); // expect last-idx-major
                    std::array<InternalLabelType,s_nTemporalCliqueOrder> aCliqueLabels{};
                    constexpr size_t nAssignCount = 1u<<s_nTemporalCliqueOrder;
                    for(size_t nAssignIdx=0; nAssignIdx<nAssignCount; ++nAssignIdx) {
                        for(size_t nVarIdx=0; nVarIdx<s_nTemporalCliqueOrder; ++nVarIdx)
                            aCliqueLabels[nVarIdx] = (nAssignIdx&(1<<nVarIdx))?s_nForegroundLabelIdx:s_nBackgroundLabelIdx;
                        float fCliqueCoeffs = 0.0f;
                        for(size_t nOffsetLayerIdx=1; nOffsetLayerIdx<nTemporalLayerCount; ++nOffsetLayerIdx) {
                            if(aCliqueLabels[nOffsetLayerIdx]!=aCliqueLabels[nOffsetLayerIdx-1u]) {
                                const size_t nPreviousLayer = nOffsetLayerIdx;
                                const size_t nCurrLayer = nOffsetLayerIdx-1u;
                                const cv::Mat_<uchar>& oPrevToCurrTempDiffMap = aaTempDiff[nCamIdx][nCurrLayer];
                                lvDbgAssert(oPrevToCurrTempDiffMap.total()==nLayerSize && oPrevToCurrTempDiffMap.isContinuous());
                                const size_t nPreviousMapIdx = vnMapIdxs[nPreviousLayer];
                                const int nTemporalGradient = int(oPrevToCurrTempDiffMap.data[nPreviousMapIdx]);
                                const float fTemporalScaleFact = m_aLabelSimCostGradFactLUT.eval_raw(nTemporalGradient);
                                fCliqueCoeffs += fTemporalScaleFact;
                                if(bDisplayDbgMaps) {
                                    const size_t nCurrStackedIdx = nLayerSize*nCurrLayer+vnMapIdxs[nCurrLayer];
                                    lvDbgAssert(nCurrStackedIdx<aTempCoeffsDbgImgs[nCamIdx].total()-nLayerSize);
                                    const size_t nCurrChIdx = (nOffsetLayerIdx%2)?2:0;
                                    ((cv::Vec3f*)aTempCoeffsDbgImgs[nCamIdx].data)[nCurrStackedIdx][nCurrChIdx] += fTemporalScaleFact;
                                    const size_t nPreviousStackedIdx = nLayerSize*nPreviousLayer+vnMapIdxs[nPreviousLayer];
                                    lvDbgAssert(nPreviousStackedIdx<aTempCoeffsDbgImgs[nCamIdx].total());
                                    const size_t nNextChIdx = (nOffsetLayerIdx%2)?0:2;
                                    ((cv::Vec3f*)aTempCoeffsDbgImgs[nCamIdx].data)[nPreviousStackedIdx][nNextChIdx] += fTemporalScaleFact;
                                }
                            }
                        }
                        vTemporalResegmLUT(aCliqueLabels.begin()) = cost_cast(fCliqueCoeffs*SEGMMATCH_LBLSIM_RESEGM_SCALE_CST);
                    }
                }
            }
        }
        m_pResegmModel->finalize(); // needed due to possible temporal clique updates
    }
#endif //SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
    lvDbgAssert(nCliqueIdx<=m_nResegmCliqueCount);
    if(bDisplayDbgMaps) {
        for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
            cv::normalize(aFGLogProbMap[nCamIdx],aFGLogProbMap[nCamIdx],1,0,cv::NORM_MINMAX);
            cv::normalize(aBGLogProbMap[nCamIdx],aBGLogProbMap[nCamIdx],1,0,cv::NORM_MINMAX);
            cv::imshow(std::string("aFGLogProbMap-c")+std::to_string(nCamIdx),aFGLogProbMap[nCamIdx]);
            cv::imshow(std::string("aBGLogProbMap-c")+std::to_string(nCamIdx),aBGLogProbMap[nCamIdx]);
            cv::normalize(aFGDistMap[nCamIdx],aFGDistMap[nCamIdx],1,0,cv::NORM_MINMAX);
            cv::normalize(aBGDistMap[nCamIdx],aBGDistMap[nCamIdx],1,0,cv::NORM_MINMAX);
            cv::Mat oClusterLabelsDisplay = lv::getUniqueColorMap(aGMMClusterLabels[nCamIdx]);
            cv::imshow(std::string("aGMMClusterLabels-c")+std::to_string(nCamIdx),oClusterLabelsDisplay);
        #if SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
            if(bInit && nTemporalLayerCount>1u && m_nFramesProcessed>=(nTemporalLayerCount-1u)) {
                cv::normalize(aTempCoeffsDbgImgs[nCamIdx],aTempCoeffsDbgImgs[nCamIdx],1,0,cv::NORM_MINMAX);
                cv::Mat oCurrStackedInputImages = m_aStackedInputImages[nCamIdx].clone();
                lvDbgAssert(oCurrStackedInputImages.channels()==1 || oCurrStackedInputImages.channels()==3);
                if(oCurrStackedInputImages.channels()==1)
                    cv::cvtColor(oCurrStackedInputImages,oCurrStackedInputImages,cv::COLOR_GRAY2BGR);
                oCurrStackedInputImages.convertTo(oCurrStackedInputImages,CV_32F,1.0/255);
                cv::Mat oCurrTempCoeefsDbgImg = aTempCoeffsDbgImgs[nCamIdx]*0.75+oCurrStackedInputImages*0.25;
                cv::Mat tmp;
                cv::resize(aTempCoeffsDbgImgs[nCamIdx],tmp,cv::Size(),0.5,0.5);
                cv::imshow(std::string("raw-aTempCoeffsDbgImgs-c")+std::to_string(nCamIdx),tmp);
                cv::resize(oCurrTempCoeefsDbgImg,tmp,cv::Size(),0.5,0.5);
                cv::imshow(std::string("aTempCoeffsDbgImgs-c")+std::to_string(nCamIdx),tmp);
                cv::resize(aTempLinkDbgImgs[nCamIdx],tmp,cv::Size(),0.75,0.75);
                cv::imshow(std::string("dbg-resegm-init-")+std::to_string(nCamIdx),tmp);
                for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx)
                    cv::imshow(std::string("aaTempDiff-c")+std::to_string(nCamIdx)+"-t"+std::to_string(nLayerIdx),aaTempDiff[nCamIdx][nLayerIdx]);
            }
        #endif //SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
        }
        //cv::Mat oStereoDispMap;
        //m_oSuperStackedStereoLabeling.convertTo(oStereoDispMap,CV_32F,1.0/m_nStereoLabels);
        /*if(oFGProbMap.rows>1024 || oFGProbMap.cols>1024) {
            cv::resize(oStereoDispMap,oStereoDispMap,cv::Size(),0.25,0.25);
            cv::resize(oFGProbMap,oFGProbMap,cv::Size(),0.25,0.25);
            cv::resize(oBGProbMap,oBGProbMap,cv::Size(),0.25,0.25);
            cv::resize(oFGDistMap,oFGDistMap,cv::Size(),0.25,0.25);
            cv::resize(oBGDistMap,oBGDistMap,cv::Size(),0.25,0.25);
        }*/
        //cv::imshow("oStereoDispMap",oStereoDispMap);
        //cv::imshow("oFGDistMap",oFGDistMap);
        //cv::imshow("oBGDistMap",oBGDistMap);
        cv::waitKey(1);
    }
    lvLog_(4,"Resegm graph model energy terms update completed in %f second(s).",oLocalTimer.tock());
}

void SegmMatcher::GraphModelData::calcFeatures(const MatArrayIn& aInputs, cv::Mat* pFeaturesPacket) {
    static_assert(s_nInputArraySize==4 && getCameraCount()==2,"lots of hardcoded indices below");
    lvDbgExceptionWatch;
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        const cv::Mat& oInputImg = aInputs[nCamIdx*InputPackOffset+InputPackOffset_Img];
        lvAssert__(oInputImg.dims==2 && m_oGridSize==oInputImg.size(),"input image in array at index=%d had the wrong size",(int)nCamIdx);
        lvAssert_(oInputImg.type()==CV_8UC1 || oInputImg.type()==CV_8UC3,"unexpected input image type");
        const cv::Mat& oInputMask = aInputs[nCamIdx*InputPackOffset+InputPackOffset_Mask];
        lvAssert__(oInputMask.dims==2 && m_oGridSize==oInputMask.size(),"input mask in array at index=%d had the wrong size",(int)nCamIdx);
        lvAssert_(oInputMask.type()==CV_8UC1,"unexpected input mask type");
    }
    m_vTempFeatures.resize(FeatPackSize); // if this function was not called externally, features will be swapped from this temporary to the internal array
    calcImageFeatures(CamArray<cv::Mat>{aInputs[InputPack_LeftImg],aInputs[InputPack_RightImg]},m_vTempFeatures);
    calcShapeFeatures(CamArray<cv::Mat_<InternalLabelType>>{aInputs[InputPack_LeftMask],aInputs[InputPack_RightMask]},m_vTempFeatures);
    for(cv::Mat& oFeatMap : m_vTempFeatures)
        lvAssert_(oFeatMap.isContinuous(),"internal func used non-continuous data block for feature maps");
    if(pFeaturesPacket)
        *pFeaturesPacket = lv::packData(m_vTempFeatures,&m_vLatestFeatPackInfo);
    else { // fill pack info manually
        m_vLatestFeatPackInfo.resize(m_vTempFeatures.size());
        for(size_t nFeatMapIdx=0; nFeatMapIdx<m_vTempFeatures.size(); ++nFeatMapIdx)
            m_vLatestFeatPackInfo[nFeatMapIdx] = lv::MatInfo(m_vTempFeatures[nFeatMapIdx]);
    }
    if(m_vExpectedFeatPackInfo.empty())
        m_vExpectedFeatPackInfo = m_vLatestFeatPackInfo;
    lvAssert_(m_vLatestFeatPackInfo==m_vExpectedFeatPackInfo,"packed features info mismatch (should stay constant for all inputs)");
}

void SegmMatcher::GraphModelData::calcImageFeatures(const CamArray<cv::Mat>& aInputImages, std::vector<cv::Mat>& vFeatures) {
    static_assert(getCameraCount()==2,"bad input image array size");
    lvDbgExceptionWatch;
    for(size_t nInputIdx=0; nInputIdx<aInputImages.size(); ++nInputIdx) {
        lvDbgAssert__(aInputImages[nInputIdx].dims==2 && m_oGridSize==aInputImages[nInputIdx].size(),"input at index=%d had the wrong size",(int)nInputIdx);
        lvDbgAssert_(aInputImages[nInputIdx].type()==CV_8UC1 || aInputImages[nInputIdx].type()==CV_8UC3,"unexpected input image type");
    }
    lvDbgAssert_(vFeatures.size()==FeatPackSize,"unexpected feat vec size");
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    lvLog(3,"Calculating image features maps...");
    const int nWinRadius = (int)m_nGridBorderSize;
    const int nWinSize = nWinRadius*2+1;
    CamArray<cv::Mat> aEnlargedInput;
#if SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
    lvIgnore(nWinSize);
    CamArray<cv::Mat_<float>> aEnlargedDescs,aDescs;
    const int nPatchSize = SEGMMATCH_DEFAULT_DESC_PATCH_SIZE;
#else //!SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
    CamArray<cv::Mat_<uchar>> aEnlargedROIs;
    const int nPatchSize = nWinSize;
#endif //SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
    lvAssert_((nPatchSize%2)==1,"patch sizes must be odd");
    lv::StopWatch oLocalTimer;
#if USING_OPENMP
    //#pragma omp parallel for num_threads(getCameraCount())
#endif //USING_OPENMP
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        cv::copyMakeBorder(aInputImages[nCamIdx],aEnlargedInput[nCamIdx],nWinRadius,nWinRadius,nWinRadius,nWinRadius,cv::BORDER_DEFAULT);
    #if SEGMMATCH_CONFIG_USE_MI_AFFINITY
        if(aEnlargedInput[nCamIdx].channels()==3)
            cv::cvtColor(aEnlargedInput[nCamIdx],aEnlargedInput[nCamIdx],cv::COLOR_BGR2GRAY);
        cv::copyMakeBorder(m_aROIs[nCamIdx],aEnlargedROIs[nCamIdx],nWinRadius,nWinRadius,nWinRadius,nWinRadius,cv::BORDER_CONSTANT,cv::Scalar(0));
    #elif SEGMMATCH_CONFIG_USE_SSQDIFF_AFFINITY
        if(aEnlargedInput[nCamIdx].channels()==3)
            cv::cvtColor(aEnlargedInput[nCamIdx],aEnlargedInput[nCamIdx],cv::COLOR_BGR2GRAY);
        aEnlargedInput[nCamIdx].convertTo(aEnlargedInput[nCamIdx],CV_64F,(1.0/UCHAR_MAX)/nWinSize);
        aEnlargedInput[nCamIdx] -= cv::mean(aEnlargedInput[nCamIdx])[0];
        cv::copyMakeBorder(m_aROIs[nCamIdx],aEnlargedROIs[nCamIdx],nWinRadius,nWinRadius,nWinRadius,nWinRadius,cv::BORDER_CONSTANT,cv::Scalar(0));
    #elif SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
        lvLog_(3,"\tcam[%d] image descriptors...",(int)nCamIdx);
        m_pImgDescExtractor->compute2(aEnlargedInput[nCamIdx],aEnlargedDescs[nCamIdx]);
        lvDbgAssert(aEnlargedDescs[nCamIdx].dims==3 && aEnlargedDescs[nCamIdx].size[0]==nRows+nWinRadius*2 && aEnlargedDescs[nCamIdx].size[1]==nCols+nWinRadius*2);
        std::vector<cv::Range> vRanges(size_t(3),cv::Range::all());
        vRanges[0] = cv::Range(nWinRadius,nRows+nWinRadius);
        vRanges[1] = cv::Range(nWinRadius,nCols+nWinRadius);
        aEnlargedDescs[nCamIdx](vRanges.data()).copyTo(aDescs[nCamIdx]); // copy to avoid bugs when reshaping non-continuous data
        lvDbgAssert(aDescs[nCamIdx].dims==3 && aDescs[nCamIdx].size[0]==nRows && aDescs[nCamIdx].size[1]==nCols);
        lvDbgAssert(std::equal(aDescs[nCamIdx].ptr<float>(0,0),aDescs[nCamIdx].ptr<float>(0,0)+aDescs[nCamIdx].size[2],aEnlargedDescs[nCamIdx].ptr<float>(nWinRadius,nWinRadius)));
    #if SEGMMATCH_CONFIG_USE_ROOT_SIFT_DESCS
        const size_t nDescSize = size_t(aDescs[nCamIdx].size[2]);
        for(size_t nDescIdx=0; nDescIdx<aDescs[nCamIdx].total(); nDescIdx+=nDescSize)
            lv::rootSIFT(((float*)aDescs[nCamIdx].data)+nDescIdx,nDescSize);
    #endif //SEGMMATCH_CONFIG_USE_ROOT_SIFT_DESCS
    #endif //SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
        lvLog_(3,"\tcam[%d] image gradient magnitudes...",(int)nCamIdx);
        cv::Mat oBlurredInput,oGrayInput;
        cv::GaussianBlur(aInputImages[nCamIdx],oBlurredInput,cv::Size(3,3),0);
        cv::Mat oBlurredGrayInput;
        if(oBlurredInput.channels()==3) {
            cv::cvtColor(oBlurredInput,oBlurredGrayInput,cv::COLOR_BGR2GRAY);
            cv::cvtColor(aInputImages[nCamIdx],oGrayInput,cv::COLOR_BGR2GRAY);
        }
        else {
            oBlurredGrayInput = oBlurredInput;
            oGrayInput = aInputImages[nCamIdx];
        }
        cv::Mat oGradInput_X,oGradInput_Y;
        cv::Sobel(oBlurredGrayInput,oGradInput_Y,CV_16S,0,1,SEGMMATCH_DEFAULT_GRAD_KERNEL_SIZE);
        cv::Mat& oGradY = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_GradY];
        cv::normalize(cv::abs(oGradInput_Y),oGradY,255,0,cv::NORM_MINMAX,CV_8U);
        cv::Sobel(oBlurredGrayInput,oGradInput_X,CV_16S,1,0,SEGMMATCH_DEFAULT_GRAD_KERNEL_SIZE);
        cv::Mat& oGradX = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_GradX];
        cv::normalize(cv::abs(oGradInput_X),oGradX,255,0,cv::NORM_MINMAX,CV_8U);
        cv::Mat& oGradMag = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_GradMag];
        cv::addWeighted(oGradY,0.5,oGradX,0.5,0,oGradMag);
        /*cv::imshow("gradm_full",oGradMag);
        cv::imshow("gradm_0.5piv",oGradMag>SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST/2);
        cv::imshow("gradm_1.0piv",oGradMag>SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST);
        cv::imshow("gradm_2.0piv",oGradMag>SEGMMATCH_LBLSIM_COST_GRADPIVOT_CST*2);
        cv::imshow("gradm_100",oGradMag>100);
        cv::imshow("gradm_150",oGradMag>150);
        cv::waitKey(0);*/
        cv::Mat& oOptFlow = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_OptFlow];
        cv::Mat& oTempDiff = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_TempDiff];
        oOptFlow.create(m_oGridSize,CV_32FC2);
        oTempDiff.create(m_oGridSize,CV_8UC1);
        if(getTemporalLayerCount()>1u && m_nFramesProcessed>0u) {
            cv::Mat oPreviousInput;
            if(aInputImages[nCamIdx].data==m_aaInputs[0][nCamIdx*InputPackOffset+InputPackOffset_Img].data)
                oPreviousInput = m_aaInputs[1][nCamIdx*InputPackOffset+InputPackOffset_Img];
            else
                oPreviousInput = m_aaInputs[0][nCamIdx*InputPackOffset+InputPackOffset_Img];
            lvDbgAssert(lv::MatInfo(aInputImages[nCamIdx])==lv::MatInfo(oPreviousInput));
            ofdis::computeFlow(oPreviousInput,aInputImages[nCamIdx],oOptFlow);
            lvDbgAssert(m_oGridSize==oOptFlow.size && oOptFlow.type()==CV_32FC2);
            lvDbgAssert(oOptFlow.data==vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_OptFlow].data);
            cv::Mat oPreviousGrayInput;
            if(oPreviousInput.channels()==3)
                cv::cvtColor(oPreviousInput,oPreviousGrayInput,cv::COLOR_BGR2GRAY);
            else
                oPreviousGrayInput = oPreviousInput;
            cv::Mat oTempDiff_32f;
            lv::computeTemporalAbsDiff(oPreviousGrayInput,oGrayInput,oOptFlow,oTempDiff_32f);
            oTempDiff_32f.convertTo(oTempDiff,CV_8U);
            lvDbgAssert(m_oGridSize==oTempDiff.size && oTempDiff.data==vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_TempDiff].data);
            if(lv::getVerbosity()>=4) {
                cv::imshow(std::string("oOptFlow-")+std::to_string(nCamIdx),lv::getFlowColorMap(oOptFlow));
                cv::imshow(std::string("oTempDiff-")+std::to_string(nCamIdx),oTempDiff);
                cv::Mat oInputRemap;
                lv::remap_offset(oPreviousGrayInput,oInputRemap,oOptFlow,cv::INTER_LINEAR,cv::BORDER_REPLICATE);
                cv::imshow(std::string("oInputRemap-")+std::to_string(nCamIdx),oInputRemap);
                cv::waitKey(1);
            }
        }
        else {
            oOptFlow = cv::Vec2f(0.0f,0.0f);
            oTempDiff = 0u;
        }
    }
    lvLog_(3,"Image features maps computed in %f second(s).",oLocalTimer.tock());
    lvLog(3,"Calculating image affinity map...");
    const std::array<int,3> anAffinityMapDims = {nRows,nCols,(int)m_nRealStereoLabels};
    vFeatures[FeatPack_ImgAffinity].create(3,anAffinityMapDims.data(),CV_32FC1);
    cv::Mat_<float> oAffinity = vFeatures[FeatPack_ImgAffinity];
    std::vector<int> vDisparityOffsets;
    for(InternalLabelType nLabelIdx = 0; nLabelIdx<m_nRealStereoLabels; ++nLabelIdx)
        vDisparityOffsets.push_back(getOffsetValue(0,nLabelIdx));
    // note: we only create the dense affinity map for 1st cam here; affinity for 2nd cam will be deduced from it
#if SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
    lv::computeDescriptorAffinity(aDescs[0],aDescs[1],nPatchSize,oAffinity,vDisparityOffsets,lv::AffinityDist_L2,m_aROIs[0],m_aROIs[1]);
    /*cv::Mat_<float> tmp;
    lv::computeDescriptorAffinity(aDescs[0],aDescs[1],nPatchSize,tmp,vDisparityOffsets,lv::AffinityDist_L2,m_aROIs[0],m_aROIs[1],cv::Mat_<float>(),false);
    lvAssert(lv::MatInfo(tmp)==lv::MatInfo(oAffinity));
    for(int i=0; i<nRows; ++i)
        for(int j=0; j<nCols; ++j)
            for(int k=0; k<anAffinityMapDims[2]; ++k)
                    lvAssert__(std::abs(tmp(i,j,k)-oAffinity(i,j,k))<0.0001f," %d,%d,%d =  %f vs %f,   w/ roi0 = %d",i,j,k,tmp(i,j,k),oAffinity(i,j,k),(int)m_aROIs[0](i,j));*/
#elif SEGMMATCH_CONFIG_USE_MI_AFFINITY
    lv::computeImageAffinity(aEnlargedInput[0],aEnlargedInput[1],nWinSize,oAffinity,vDisparityOffsets,lv::AffinityDist_MI,aEnlargedROIs[0],aEnlargedROIs[1]);
#elif SEGMMATCH_CONFIG_USE_SSQDIFF_AFFINITY
    lv::computeImageAffinity(aEnlargedInput[0],aEnlargedInput[1],nWinSize,oAffinity,vDisparityOffsets,lv::AffinityDist_SSD,aEnlargedROIs[0],aEnlargedROIs[1]);
#endif //SEGMMATCH_CONFIG_USE_..._AFFINITY
    lvDbgAssert(lv::MatInfo(oAffinity)==lv::MatInfo(lv::MatSize(3,anAffinityMapDims.data()),CV_32FC1));
    lvDbgAssert(vFeatures[FeatPack_ImgAffinity].data==oAffinity.data);
    lvLog_(3,"Image affinity map computed in %f second(s).",oLocalTimer.tock());
    lvLog(3,"Calculating image saliency map...");
    vFeatures[FeatPack_ImgSaliency].create(2,anAffinityMapDims.data(),CV_32FC1);
    cv::Mat_<float> oSaliency = vFeatures[FeatPack_ImgSaliency];
    oSaliency = 0.0f; // default value for OOB pixels
    std::vector<float> vValidAffinityVals;
    vValidAffinityVals.reserve(m_nRealStereoLabels);
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        const int nRowIdx = oNode.nRowIdx;
        const int nColIdx = oNode.nColIdx;
        lvDbgAssert(oNode.bValidGraphNode && m_aROIs[m_nPrimaryCamIdx](nRowIdx,nColIdx)>0);
        vValidAffinityVals.resize(0);
        const float* pAffinityPtr = oAffinity.ptr<float>(nRowIdx,nColIdx);
        std::copy_if(pAffinityPtr,pAffinityPtr+m_nRealStereoLabels,std::back_inserter(vValidAffinityVals),[](float v){return v>=0.0f;});
        const float fCurrDistSparseness = vValidAffinityVals.size()>1?(float)lv::sparseness(vValidAffinityVals.data(),vValidAffinityVals.size()):0.0f;
#if SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
        const float fCurrDescSparseness = (float)lv::sparseness(aDescs[m_nPrimaryCamIdx].ptr<float>(nRowIdx,nColIdx),size_t(aDescs[m_nPrimaryCamIdx].size[2]));
        oSaliency.at<float>(nRowIdx,nColIdx) = std::max(fCurrDescSparseness,fCurrDistSparseness);
#else //!SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
        oSaliency.at<float>(nRowIdx,nColIdx) = fCurrDistSparseness;
#endif //!SEGMMATCH_CONFIG_USE_DESC_BASED_AFFINITY
    }
    cv::normalize(oSaliency,oSaliency,1,0,cv::NORM_MINMAX,-1,m_aROIs[m_nPrimaryCamIdx]);
    lvDbgExec( // cv::normalize leftover fp errors are sometimes awful; need to 0-max when using map
        for(int nRowIdx=0; nRowIdx<oSaliency.rows; ++nRowIdx)
            for(int nColIdx=0; nColIdx<oSaliency.cols; ++nColIdx)
                lvDbgAssert((oSaliency.at<float>(nRowIdx,nColIdx)>=-1e-6f && oSaliency.at<float>(nRowIdx,nColIdx)<=1.0f+1e-6f) || m_aROIs[m_nPrimaryCamIdx](nRowIdx,nColIdx)==0);
    );
#if SEGMMATCH_CONFIG_USE_SALIENT_MAP_BORDR
    cv::multiply(oSaliency,cv::Mat_<float>(oSaliency.size(),1.0f).setTo(0.5f,m_aDescROIs[m_nPrimaryCamIdx]==0),oSaliency);
#endif //SEGMMATCH_CONFIG_USE_SALIENT_MAP_BORDR
    if(lv::getVerbosity()>=4) {
        cv::imshow("oSaliency_img",oSaliency);
        cv::waitKey(1);
    }
    lvLog_(3,"Image saliency map computed in %f second(s).",oLocalTimer.tock());
    /*if(m_pDisplayHelper) {
        std::vector<std::pair<cv::Mat,std::string>> vAffMaps;
        for(InternalLabelType nLabelIdx=0; nLabelIdx<m_nRealStereoLabels; ++nLabelIdx)
            vAffMaps.push_back(std::pair<cv::Mat,std::string>(lv::squeeze(lv::getSubMat(oAffinity,2,(int)nLabelIdx)),std::string()));
        m_pDisplayHelper->displayAlbumAndWaitKey(vAffMaps);
    }*/
}

void SegmMatcher::GraphModelData::calcShapeFeatures(const CamArray<cv::Mat_<InternalLabelType>>& aInputMasks, std::vector<cv::Mat>& vFeatures) {
    static_assert(getCameraCount()==2,"bad input mask array size");
    lvDbgExceptionWatch;
    for(size_t nInputIdx=0; nInputIdx<aInputMasks.size(); ++nInputIdx) {
        lvDbgAssert__(aInputMasks[nInputIdx].dims==2 && m_oGridSize==aInputMasks[nInputIdx].size(),"input at index=%d had the wrong size",(int)nInputIdx);
        lvDbgAssert_(aInputMasks[nInputIdx].type()==CV_8UC1,"unexpected input mask type");
    }
    lvDbgAssert_(vFeatures.size()==FeatPackSize,"unexpected feat vec size");
    const int nRows=(int)m_oGridSize(0),nCols=(int)m_oGridSize(1);
    lvLog(3,"Calculating shape features maps...");
    CamArray<cv::Mat_<float>> aDescs;
    const int nPatchSize = SEGMMATCH_DEFAULT_DESC_PATCH_SIZE;
    lvAssert_((nPatchSize%2)==1,"patch sizes must be odd");
    lv::StopWatch oLocalTimer;
#if USING_OPENMP
    //#pragma omp parallel for num_threads(getCameraCount())
#endif //USING_OPENMP
    for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
        const cv::Mat& oInputMask = aInputMasks[nCamIdx];
        lvLog_(3,"\tcam[%d] shape descriptors...",(int)nCamIdx);
        m_pShpDescExtractor->compute2(oInputMask,aDescs[nCamIdx]);
        lvDbgAssert(aDescs[nCamIdx].dims==3 && aDescs[nCamIdx].size[0]==nRows && aDescs[nCamIdx].size[1]==nCols);
    #if SEGMMATCH_CONFIG_USE_ROOT_SIFT_DESCS
        const size_t nDescSize = size_t(aDescs[nCamIdx].size[2]);
        for(size_t nDescIdx=0; nDescIdx<aDescs[nCamIdx].total(); nDescIdx+=nDescSize)
            lv::rootSIFT(((float*)aDescs[nCamIdx].data)+nDescIdx,nDescSize);
    #endif //SEGMMATCH_CONFIG_USE_ROOT_SIFT_DESCS
        lvLog_(3,"\tcam[%d] shape distance fields...",(int)nCamIdx);
        calcShapeDistFeatures(aInputMasks[nCamIdx],nCamIdx,vFeatures);
    }
    lvLog_(3,"Shape features maps computed in %f second(s).",oLocalTimer.tock());
    lvLog(3,"Calculating shape affinity map...");
    const std::array<int,3> anAffinityMapDims = {nRows,nCols,(int)m_nRealStereoLabels};
    vFeatures[FeatPack_ShpAffinity].create(3,anAffinityMapDims.data(),CV_32FC1);
    cv::Mat_<float> oAffinity = vFeatures[FeatPack_ShpAffinity];
    std::vector<int> vDisparityOffsets;
    for(InternalLabelType nLabelIdx = 0; nLabelIdx<m_nRealStereoLabels; ++nLabelIdx)
        vDisparityOffsets.push_back(getOffsetValue(0,nLabelIdx));
#if SEGMMATCH_CONFIG_USE_SHAPE_EMD_AFFIN
    lv::computeDescriptorAffinity(aDescs[0],aDescs[1],nPatchSize,oAffinity,vDisparityOffsets,lv::AffinityDist_EMD,m_aROIs[0],m_aROIs[1],m_pShpDescExtractor->getEMDCostMap());
#else //!SEGMMATCH_CONFIG_USE_SHAPE_EMD_AFFIN
    lv::computeDescriptorAffinity(aDescs[0],aDescs[1],nPatchSize,oAffinity,vDisparityOffsets,lv::AffinityDist_L2,m_aROIs[0],m_aROIs[1]);
#endif //!SEGMMATCH_CONFIG_USE_SHAPE_EMD_AFFIN
    lvDbgAssert(lv::MatInfo(oAffinity)==lv::MatInfo(lv::MatSize(3,anAffinityMapDims.data()),CV_32FC1));
    lvDbgAssert(vFeatures[FeatPack_ShpAffinity].data==oAffinity.data);
    lvLog_(3,"Shape affinity map computed in %f second(s).",oLocalTimer.tock());
    lvLog(3,"Calculating shape saliency map...");
    vFeatures[FeatPack_ShpSaliency].create(2,anAffinityMapDims.data(),CV_32FC1);
    cv::Mat_<float> oSaliency = vFeatures[FeatPack_ShpSaliency];
    oSaliency = 0.0f; // default value for OOB pixels
    std::vector<float> vValidAffinityVals;
    vValidAffinityVals.reserve(m_nRealStereoLabels);
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode);
        const int nRowIdx = oNode.nRowIdx;
        const int nColIdx = oNode.nColIdx;
        vValidAffinityVals.resize(0);
        const float* pAffinityPtr = oAffinity.ptr<float>(nRowIdx,nColIdx);
        std::copy_if(pAffinityPtr,pAffinityPtr+m_nRealStereoLabels,std::back_inserter(vValidAffinityVals),[](float v){return v>=0.0f;});
        const float fCurrDistSparseness = vValidAffinityVals.size()>1?(float)lv::sparseness(vValidAffinityVals.data(),vValidAffinityVals.size()):0.0f;
        const float fCurrDescSparseness = (float)lv::sparseness(aDescs[m_nPrimaryCamIdx].ptr<float>(nRowIdx,nColIdx),size_t(aDescs[m_nPrimaryCamIdx].size[2]));
        oSaliency.at<float>(nRowIdx,nColIdx) = std::max(fCurrDescSparseness,fCurrDistSparseness);
    #if SEGMMATCH_DEFAULT_SALIENT_SHP_RAD>0
        const cv::Mat& oFGDist = vFeatures[m_nPrimaryCamIdx*FeatPackOffset+FeatPackOffset_FGDist];
        const float fCurrFGDist = oFGDist.at<float>(nRowIdx,nColIdx);
        oSaliency.at<float>(nRowIdx,nColIdx) *= std::max(1-fCurrFGDist/SEGMMATCH_DEFAULT_SALIENT_SHP_RAD,0.0f);
    #endif //SEGMMATCH_DEFAULT_SALIENT_SHP_RAD>0
    }
    cv::normalize(oSaliency,oSaliency,1,0,cv::NORM_MINMAX,-1,m_aROIs[m_nPrimaryCamIdx]);
    lvDbgExec( // cv::normalize leftover fp errors are sometimes awful; need to 0-max when using map
        for(int nRowIdx=0; nRowIdx<oSaliency.rows; ++nRowIdx)
            for(int nColIdx=0; nColIdx<oSaliency.cols; ++nColIdx)
                lvDbgAssert((oSaliency.at<float>(nRowIdx,nColIdx)>=-1e-6f && oSaliency.at<float>(nRowIdx,nColIdx)<=1.0f+1e-6f) || m_aROIs[m_nPrimaryCamIdx](nRowIdx,nColIdx)==0);
    );
#if SEGMMATCH_CONFIG_USE_SALIENT_MAP_BORDR
    cv::multiply(oSaliency,cv::Mat_<float>(oSaliency.size(),1.0f).setTo(0.5f,m_aDescROIs[m_nPrimaryCamIdx]==0),oSaliency);
#endif //SEGMMATCH_CONFIG_USE_SALIENT_MAP_BORDR
    if(lv::getVerbosity()>=4) {
        cv::imshow("oSaliency_shp",oSaliency);
        cv::waitKey(1);
    }
    lvLog_(3,"Shape saliency map computed in %f second(s).",oLocalTimer.tock());
    /*if(m_pDisplayHelper) {
        std::vector<std::pair<cv::Mat,std::string>> vAffMaps;
        for(InternalLabelType nLabelIdx=0; nLabelIdx<m_nRealStereoLabels; ++nLabelIdx)
            vAffMaps.push_back(std::pair<cv::Mat,std::string>(lv::squeeze(lv::getSubMat(oAffinity,2,(int)nLabelIdx)),std::string()));
        m_pDisplayHelper->displayAlbumAndWaitKey(vAffMaps);
    }*/
}

void SegmMatcher::GraphModelData::calcShapeDistFeatures(const cv::Mat_<InternalLabelType>& oInputMask, size_t nCamIdx, std::vector<cv::Mat>& vFeatures) {
    lvDbgExceptionWatch;
    lvDbgAssert_(nCamIdx<getCameraCount(),"bad input cam index");
    lvDbgAssert_(oInputMask.dims==2 && m_oGridSize==oInputMask.size(),"input had the wrong size");
    lvDbgAssert_(oInputMask.type()==CV_8UC1,"unexpected input mask type");
    lvDbgAssert_(vFeatures.size()==FeatPackSize,"unexpected feat vec size");
    cv::Mat& oFGDist = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_FGDist];
    cv::distanceTransform(oInputMask==0,oFGDist,cv::DIST_L2,cv::DIST_MASK_PRECISE,CV_32F);
    cv::exp((-1.0f/SEGMMATCH_SHPDIST_PX_MAX_CST)*oFGDist,oFGDist);
    cv::divide(1.0,oFGDist,oFGDist);
    oFGDist -= 1.0f;
    cv::min(oFGDist,SEGMMATCH_SHPDIST_PX_MAX_CST,oFGDist);
    oFGDist.setTo(SEGMMATCH_SHPDIST_PX_MAX_CST,oFGDist<0.0f);
    cv::Mat& oBGDist = vFeatures[nCamIdx*FeatPackOffset+FeatPackOffset_BGDist];
    cv::distanceTransform(oInputMask>0,oBGDist,cv::DIST_L2,cv::DIST_MASK_PRECISE,CV_32F);
    cv::exp((-1.0f/SEGMMATCH_SHPDIST_PX_MAX_CST)*oBGDist,oBGDist);
    cv::divide(1.0,oBGDist,oBGDist);
    oBGDist -= 1.0f;
    cv::min(oBGDist,SEGMMATCH_SHPDIST_PX_MAX_CST,oBGDist);
    oBGDist.setTo(SEGMMATCH_SHPDIST_PX_MAX_CST,oBGDist<0.0f);
}

void SegmMatcher::GraphModelData::initGaussianMixtureParams(const cv::Mat& oInput, const cv::Mat& oMask, const cv::Mat& oROI, size_t nCamIdx) {
    if(oInput.channels()==1)
        lv::initGaussianMixtureParams(oInput,oMask,m_aBGModels_1ch[nCamIdx],m_aFGModels_1ch[nCamIdx],oROI);
    else // 3ch
        lv::initGaussianMixtureParams(oInput,oMask,m_aBGModels_3ch[nCamIdx],m_aFGModels_3ch[nCamIdx],oROI);
}

void SegmMatcher::GraphModelData::assignGaussianMixtureComponents(const cv::Mat& oInput, const cv::Mat& oMask, cv::Mat& oAssignMap, const cv::Mat& oROI, size_t nCamIdx) {
    if(oInput.channels()==1)
        lv::assignGaussianMixtureComponents(oInput,oMask,oAssignMap,m_aBGModels_1ch[nCamIdx],m_aFGModels_1ch[nCamIdx],oROI);
    else // 3ch
        lv::assignGaussianMixtureComponents(oInput,oMask,oAssignMap,m_aBGModels_3ch[nCamIdx],m_aFGModels_3ch[nCamIdx],oROI);
}

void SegmMatcher::GraphModelData::learnGaussianMixtureParams(const cv::Mat& oInput, const cv::Mat& oMask, const cv::Mat& oAssignMap, const cv::Mat& oROI, size_t nCamIdx) {
    if(oInput.channels()==1)
        lv::learnGaussianMixtureParams(oInput,oMask,oAssignMap,m_aBGModels_1ch[nCamIdx],m_aFGModels_1ch[nCamIdx],oROI);
    else // 3ch
        lv::learnGaussianMixtureParams(oInput,oMask,oAssignMap,m_aBGModels_3ch[nCamIdx],m_aFGModels_3ch[nCamIdx],oROI);
}

double SegmMatcher::GraphModelData::getGMMFGProb(const cv::Mat& oInput, size_t nElemIdx, size_t nCamIdx) const {
    lvDbgAssert(!oInput.empty() && oInput.depth()==CV_8U && nElemIdx<oInput.total());
    if(oInput.channels()==1)
        return m_aFGModels_1ch[nCamIdx](oInput.data+nElemIdx);
    else // 3ch
        return m_aFGModels_3ch[nCamIdx](oInput.data+nElemIdx*oInput.channels());
}

double SegmMatcher::GraphModelData::getGMMBGProb(const cv::Mat& oInput, size_t nElemIdx, size_t nCamIdx) const {
    lvDbgAssert(!oInput.empty() && oInput.depth()==CV_8U && nElemIdx<oInput.total());
    if(oInput.channels()==1)
        return m_aBGModels_1ch[nCamIdx](oInput.data+nElemIdx);
    else // 3ch
        return m_aBGModels_3ch[nCamIdx](oInput.data+nElemIdx*oInput.channels());
}

void SegmMatcher::GraphModelData::setNextFeatures(const cv::Mat& oPackedFeatures) {
    lvDbgExceptionWatch;
    lvAssert_(!oPackedFeatures.empty() && oPackedFeatures.isContinuous(),"features packet must be non-empty and continuous");
    if(m_vExpectedFeatPackInfo.empty()) {
        m_vExpectedFeatPackInfo.resize(FeatPackSize);
        // hard-coded fill for matinfo types; if features change internally, this list may also need to be updated
        for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_FGDist] = lv::MatInfo(m_oGridSize,CV_32FC1);
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_BGDist] = lv::MatInfo(m_oGridSize,CV_32FC1);
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_GradY] = lv::MatInfo(m_oGridSize,CV_8UC1);
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_GradX] = lv::MatInfo(m_oGridSize,CV_8UC1);
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_GradMag] = lv::MatInfo(m_oGridSize,CV_8UC1);
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_OptFlow] = lv::MatInfo(m_oGridSize,CV_32FC2);
            m_vExpectedFeatPackInfo[nCamIdx*FeatPackOffset+FeatPackOffset_TempDiff] = lv::MatInfo(m_oGridSize,CV_8UC1);
        }
        m_vExpectedFeatPackInfo[FeatPack_ImgSaliency] = lv::MatInfo(m_oGridSize,CV_32FC1);
        m_vExpectedFeatPackInfo[FeatPack_ShpSaliency] = lv::MatInfo(m_oGridSize,CV_32FC1);
        m_vExpectedFeatPackInfo[FeatPack_ImgAffinity] = lv::MatInfo(std::array<int,3>{(int)m_oGridSize(0),(int)m_oGridSize(1),(int)m_nRealStereoLabels},CV_32FC1);
        m_vExpectedFeatPackInfo[FeatPack_ShpAffinity] = lv::MatInfo(std::array<int,3>{(int)m_oGridSize(0),(int)m_oGridSize(1),(int)m_nRealStereoLabels},CV_32FC1);
    }
    const std::vector<cv::Mat> vLatestUnpackedFeatures = lv::unpackData(oPackedFeatures,m_vExpectedFeatPackInfo);
    m_vLoadedFeatures.resize(FeatPackSize);
    for(size_t nFeatsIdx=0; nFeatsIdx<vLatestUnpackedFeatures.size(); ++nFeatsIdx)
        vLatestUnpackedFeatures[nFeatsIdx].copyTo(m_vLoadedFeatures[nFeatsIdx]); // makes internal copy, user can discard packet before next 'apply' call
    m_bUsePrecalcFeaturesNext = true;
}

inline SegmMatcher::OutputLabelType SegmMatcher::GraphModelData::getRealLabel(InternalLabelType nLabel) const {
    lvDbgExceptionWatch;
    lvDbgAssert(nLabel<m_vStereoLabels.size());
    return m_vStereoLabels[nLabel];
}

inline SegmMatcher::InternalLabelType SegmMatcher::GraphModelData::getInternalLabel(OutputLabelType nRealLabel) const {
    lvDbgExceptionWatch;
    lvDbgAssert(nRealLabel==s_nOccludedLabel || nRealLabel==s_nDontCareLabel);
    lvDbgAssert(nRealLabel>=(OutputLabelType)m_nMinDispOffset && nRealLabel<=(OutputLabelType)m_nMaxDispOffset);
    lvDbgAssert(((nRealLabel-m_nMinDispOffset)%m_nDispOffsetStep)==0);
    return (InternalLabelType)std::distance(m_vStereoLabels.begin(),std::find(m_vStereoLabels.begin(),m_vStereoLabels.end(),nRealLabel));
}

inline int SegmMatcher::GraphModelData::getOffsetValue(size_t nCamIdx, InternalLabelType nLabel) const {
    static_assert(getCameraCount()==2,"bad hardcoded offset sign");
    lvDbgExceptionWatch;
    lvDbgAssert(nCamIdx==size_t(0) || nCamIdx==size_t(1));
    lvDbgAssert(nLabel<m_nRealStereoLabels);
    const OutputLabelType nRealLabel = getRealLabel(nLabel);
    lvDbgAssert((int)nRealLabel>=(int)m_nMinDispOffset && (int)nRealLabel<=(int)m_nMaxDispOffset);
    return (nCamIdx==size_t(0))?(-nRealLabel):(nRealLabel);
}

inline int SegmMatcher::GraphModelData::getOffsetColIdx(size_t nCamIdx, int nColIdx, InternalLabelType nLabel) const {
    lvDbgExceptionWatch;
    lvDbgAssert(nColIdx>=0 && nColIdx<(int)m_oGridSize[1]);
    return nColIdx+getOffsetValue(nCamIdx,nLabel);
}

inline SegmMatcher::AssocCountType SegmMatcher::GraphModelData::getAssocCount(int nRowIdx, int nColIdx) const {
    lvDbgExceptionWatch;
    lvDbgAssert(nRowIdx>=0 && nRowIdx<(int)m_oGridSize[0]);
    lvDbgAssert(m_nPrimaryCamIdx==1 || (nColIdx>=-(int)m_nMaxDispOffset && nColIdx<(int)m_oGridSize[1]));
    lvDbgAssert(m_nPrimaryCamIdx==0 || (nColIdx>=0 && nColIdx<int(m_oGridSize[1]+m_nMaxDispOffset)));
    lvDbgAssert(m_oAssocCounts.dims==2 && !m_oAssocCounts.empty() && m_oAssocCounts.isContinuous());
    const size_t nMapOffset = ((m_nPrimaryCamIdx==size_t(1))?size_t(0):m_nMaxDispOffset);
    return ((AssocCountType*)m_oAssocCounts.data)[nRowIdx*m_oAssocCounts.cols + (nColIdx+nMapOffset)/m_nDispOffsetStep];
}

inline void SegmMatcher::GraphModelData::addAssoc(int nRowIdx, int nColIdx, InternalLabelType nLabel) const {
    static_assert(getCameraCount()==2,"bad hardcoded assoc range check");
    lvDbgExceptionWatch;
    lvDbgAssert(nLabel<m_nDontCareLabelIdx);
    lvDbgAssert(nRowIdx>=0 && nRowIdx<(int)m_oGridSize[0] && nColIdx>=0 && nColIdx<(int)m_oGridSize[1]);
    const int nAssocColIdx = getOffsetColIdx(m_nPrimaryCamIdx,nColIdx,nLabel);
    lvDbgAssert(m_nPrimaryCamIdx==1 || (nAssocColIdx<=int(nColIdx-m_nMinDispOffset) && nAssocColIdx>=int(nColIdx-m_nMaxDispOffset)));
    lvDbgAssert(m_nPrimaryCamIdx==0 || (nAssocColIdx>=int(nColIdx+m_nMinDispOffset) && nAssocColIdx<=int(nColIdx+m_nMaxDispOffset)));
    const size_t nMapOffset = ((m_nPrimaryCamIdx==size_t(1))?size_t(0):m_nMaxDispOffset);
    lvDbgAssert(m_oAssocMap.dims==3 && !m_oAssocMap.empty() && m_oAssocMap.isContinuous());
    AssocIdxType* const pAssocList = ((AssocIdxType*)m_oAssocMap.data)+(nRowIdx*m_oAssocMap.size[1] + (nAssocColIdx+nMapOffset)/m_nDispOffsetStep)*m_oAssocMap.size[2];
    lvDbgAssert(pAssocList==m_oAssocMap.ptr<AssocIdxType>(nRowIdx,(nAssocColIdx+nMapOffset)/m_nDispOffsetStep));
    const size_t nListOffset = size_t(nLabel)*m_nDispOffsetStep + nColIdx%m_nDispOffsetStep;
    lvDbgAssert((int)nListOffset<m_oAssocMap.size[2]);
    lvDbgAssert(pAssocList[nListOffset]==AssocIdxType(-1));
    pAssocList[nListOffset] = AssocIdxType(nColIdx);
    lvDbgAssert(m_oAssocCounts.dims==2 && !m_oAssocCounts.empty() && m_oAssocCounts.isContinuous());
    AssocCountType& nAssocCount = ((AssocCountType*)m_oAssocCounts.data)[nRowIdx*m_oAssocCounts.cols + (nAssocColIdx+nMapOffset)/m_nDispOffsetStep];
    lvDbgAssert((&nAssocCount)==&m_oAssocCounts(nRowIdx,int((nAssocColIdx+nMapOffset)/m_nDispOffsetStep)));
    lvDbgAssert(nAssocCount<std::numeric_limits<AssocCountType>::max());
    ++nAssocCount;
}

inline void SegmMatcher::GraphModelData::removeAssoc(int nRowIdx, int nColIdx, InternalLabelType nLabel) const {
    static_assert(getCameraCount()==2,"bad hardcoded assoc range check");
    lvDbgExceptionWatch;
    lvDbgAssert(nLabel<m_nDontCareLabelIdx);
    lvDbgAssert(nRowIdx>=0 && nRowIdx<(int)m_oGridSize[0] && nColIdx>=0 && nColIdx<(int)m_oGridSize[1]);
    const int nAssocColIdx = getOffsetColIdx(m_nPrimaryCamIdx,nColIdx,nLabel);
    lvDbgAssert(m_nPrimaryCamIdx==1 || (nAssocColIdx<=int(nColIdx-m_nMinDispOffset) && nAssocColIdx>=int(nColIdx-m_nMaxDispOffset)));
    lvDbgAssert(m_nPrimaryCamIdx==0 || (nAssocColIdx>=int(nColIdx+m_nMinDispOffset) && nAssocColIdx<=int(nColIdx+m_nMaxDispOffset)));
    const size_t nMapOffset = ((m_nPrimaryCamIdx==size_t(1))?size_t(0):m_nMaxDispOffset);
    lvDbgAssert(m_oAssocMap.dims==3 && !m_oAssocMap.empty() && m_oAssocMap.isContinuous());
    AssocIdxType* const pAssocList = ((AssocIdxType*)m_oAssocMap.data)+(nRowIdx*m_oAssocMap.size[1] + (nAssocColIdx+nMapOffset)/m_nDispOffsetStep)*m_oAssocMap.size[2];
    lvDbgAssert(pAssocList==m_oAssocMap.ptr<AssocIdxType>(nRowIdx,(nAssocColIdx+nMapOffset)/m_nDispOffsetStep));
    const size_t nListOffset = size_t(nLabel)*m_nDispOffsetStep + nColIdx%m_nDispOffsetStep;
    lvDbgAssert((int)nListOffset<m_oAssocMap.size[2]);
    lvDbgAssert(pAssocList[nListOffset]==AssocIdxType(nColIdx));
    pAssocList[nListOffset] = AssocIdxType(-1);
    lvDbgAssert(m_oAssocCounts.dims==2 && !m_oAssocCounts.empty() && m_oAssocCounts.isContinuous());
    AssocCountType& nAssocCount = ((AssocCountType*)m_oAssocCounts.data)[nRowIdx*m_oAssocCounts.cols + (nAssocColIdx+nMapOffset)/m_nDispOffsetStep];
    lvDbgAssert((&nAssocCount)==&m_oAssocCounts(nRowIdx,int((nAssocColIdx+nMapOffset)/m_nDispOffsetStep)));
    lvDbgAssert(nAssocCount>AssocCountType(0));
    --nAssocCount;
}

inline SegmMatcher::ValueType SegmMatcher::GraphModelData::calcAddAssocCost(int nRowIdx, int nColIdx, InternalLabelType nLabel) const {
    static_assert(getCameraCount()==2,"bad hardcoded assoc range check");
    lvDbgExceptionWatch;
    lvDbgAssert(nRowIdx>=0 && nRowIdx<(int)m_oGridSize[0] && nColIdx>=0 && nColIdx<(int)m_oGridSize[1]);
    if(nLabel<m_nDontCareLabelIdx) {
        const int nAssocColIdx = getOffsetColIdx(m_nPrimaryCamIdx,nColIdx,nLabel);
        lvDbgAssert(m_nPrimaryCamIdx==1 || (nAssocColIdx<=int(nColIdx-m_nMinDispOffset) && nAssocColIdx>=int(nColIdx-m_nMaxDispOffset)));
        lvDbgAssert(m_nPrimaryCamIdx==0 || (nAssocColIdx>=int(nColIdx+m_nMinDispOffset) && nAssocColIdx<=int(nColIdx+m_nMaxDispOffset)));
        const AssocCountType nAssocCount = getAssocCount(nRowIdx,nAssocColIdx);
        lvDbgAssert(nAssocCount<m_aAssocCostApproxAddLUT.size());
        return m_aAssocCostApproxAddLUT[nAssocCount];
    }
    return cost_cast(0);
}

inline SegmMatcher::ValueType SegmMatcher::GraphModelData::calcRemoveAssocCost(int nRowIdx, int nColIdx, InternalLabelType nLabel) const {
    static_assert(getCameraCount()==2,"bad hardcoded assoc range check");
    lvDbgExceptionWatch;
    lvDbgAssert(nRowIdx>=0 && nRowIdx<(int)m_oGridSize[0] && nColIdx>=0 && nColIdx<(int)m_oGridSize[1]);
    if(nLabel<m_nDontCareLabelIdx) {
        const int nAssocColIdx = getOffsetColIdx(m_nPrimaryCamIdx,nColIdx,nLabel);
        lvDbgAssert(m_nPrimaryCamIdx==1 || (nAssocColIdx<=int(nColIdx-m_nMinDispOffset) && nAssocColIdx>=int(nColIdx-m_nMaxDispOffset)));
        lvDbgAssert(m_nPrimaryCamIdx==0 || (nAssocColIdx>=int(nColIdx+m_nMinDispOffset) && nAssocColIdx<=int(nColIdx+m_nMaxDispOffset)));
        const AssocCountType nAssocCount = getAssocCount(nRowIdx,nAssocColIdx);
        lvDbgAssert(nAssocCount>0); // cannot be zero, must have at least an association in order to remove it
        lvDbgAssert(nAssocCount<m_aAssocCostApproxAddLUT.size());
        return m_aAssocCostApproxRemLUT[nAssocCount];
    }
    return -cost_cast(0);
}

SegmMatcher::ValueType SegmMatcher::GraphModelData::calcTotalAssocCost() const {
    lvDbgExceptionWatch;
    lvDbgAssert(m_oGridSize.total()==m_vStereoNodeMap.size());
    ValueType tEnergy = cost_cast(0);
    const int nColIdxStart = ((m_nPrimaryCamIdx==size_t(1))?int(m_nMinDispOffset):-int(m_nMaxDispOffset));
    const int nColIdxEnd = ((m_nPrimaryCamIdx==size_t(1))?int(m_oGridSize[1]+m_nMaxDispOffset):int(m_oGridSize[1]-m_nMinDispOffset));
    for(int nRowIdx=0; nRowIdx<(int)m_oGridSize[0]; ++nRowIdx)
        for(int nColIdx=nColIdxStart; nColIdx<nColIdxEnd; nColIdx+=m_nDispOffsetStep)
            tEnergy += m_aAssocCostRealSumLUT[getAssocCount(nRowIdx,nColIdx)];
    lvDbgAssert(tEnergy>=cost_cast(0));
    return tEnergy;
}

inline SegmMatcher::ValueType SegmMatcher::GraphModelData::calcStereoUnaryMoveCost(size_t nGraphNodeIdx, InternalLabelType nOldLabel, InternalLabelType nNewLabel) const {
    lvDbgExceptionWatch;
    lvDbgAssert(nGraphNodeIdx<m_nValidStereoGraphNodes);
    const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
    const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
    lvDbgAssert(oNode.bValidGraphNode);
    if(nOldLabel!=nNewLabel) {
        lvDbgAssert(nOldLabel<m_nStereoLabels && nNewLabel<m_nStereoLabels);
        const ValueType tAssocEnergyCost = calcRemoveAssocCost(oNode.nRowIdx,oNode.nColIdx,nOldLabel)+calcAddAssocCost(oNode.nRowIdx,oNode.nColIdx,nNewLabel);
        const ExplicitFunction& vUnaryStereoLUT = *oNode.pUnaryFunc;
        const ValueType tUnaryEnergyInit = vUnaryStereoLUT(nOldLabel);
        const ValueType tUnaryEnergyModif = vUnaryStereoLUT(nNewLabel);
        return tAssocEnergyCost+tUnaryEnergyModif-tUnaryEnergyInit;
    }
    else
        return cost_cast(0);
}

void SegmMatcher::GraphModelData::calcStereoMoveCosts(InternalLabelType nNewLabel) const {
    lvDbgExceptionWatch;
    lvDbgAssert(m_oGridSize.total()==m_vStereoNodeMap.size() && m_oGridSize.total()>1 && m_oGridSize==m_oStereoUnaryCosts.size);
    const InternalLabelType* pCurrLabeling = ((InternalLabelType*)m_aaStereoLabelings[0][m_nPrimaryCamIdx].data);
    // #### openmp here?
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
        const InternalLabelType& nCurrLabel = pCurrLabeling[nLUTNodeIdx];
        lvIgnore(oNode); lvDbgAssert(oNode.bValidGraphNode);
        lvDbgAssert(&nCurrLabel==&m_aaStereoLabelings[0][m_nPrimaryCamIdx](oNode.nRowIdx,oNode.nColIdx));
        ValueType& tUnaryCost = ((ValueType*)m_oStereoUnaryCosts.data)[nLUTNodeIdx];
        lvDbgAssert(&tUnaryCost==&m_oStereoUnaryCosts(oNode.nRowIdx,oNode.nColIdx));
        tUnaryCost = calcStereoUnaryMoveCost(nGraphNodeIdx,nCurrLabel,nNewLabel);
    }
}

void SegmMatcher::GraphModelData::calcResegmMoveCosts(InternalLabelType nNewLabel) const {
    lvDbgExceptionWatch;
    lvDbgAssert(m_oResegmUnaryCosts.rows==int(m_oGridSize[0]*getTemporalLayerCount()*getCameraCount()) && m_oResegmUnaryCosts.cols==int(m_oGridSize[1]));
    // #### openmp here?
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
        const InternalLabelType& nCurrLabel = ((InternalLabelType*)m_oSuperStackedResegmLabeling.data)[nLUTNodeIdx];
        lvDbgAssert(&nCurrLabel==&m_aaResegmLabelings[oNode.nLayerIdx][oNode.nCamIdx](oNode.nRowIdx,oNode.nColIdx));
        lvDbgAssert(nCurrLabel==s_nForegroundLabelIdx || nCurrLabel==s_nBackgroundLabelIdx);
        ValueType& tUnaryCost = ((ValueType*)m_oResegmUnaryCosts.data)[nLUTNodeIdx];
        lvDbgAssert(&tUnaryCost==&m_oResegmUnaryCosts(oNode.nRowIdx+int((oNode.nCamIdx*getTemporalLayerCount()+oNode.nLayerIdx)*m_oGridSize[0]),oNode.nColIdx));
        if(nCurrLabel!=nNewLabel) {
            const ExplicitFunction& vUnaryResegmLUT = *oNode.pUnaryFunc;
        #if SEGMMATCH_CONFIG_USE_TEMPORAL_U_CST
            const ValueType tLayerCost = cost_cast(SEGMMATCH_UNARY_COST_TEMPORAL_CST*oNode.nLayerIdx);
            const InternalLabelType& nInitLabel = ((InternalLabelType*)m_oInitSuperStackedResegmLabeling.data)[nLUTNodeIdx];
            lvDbgAssert(nInitLabel==s_nForegroundLabelIdx || nInitLabel==s_nBackgroundLabelIdx);
            const ValueType tEnergyCurr = vUnaryResegmLUT(nCurrLabel)+((nCurrLabel==nInitLabel)?cost_cast(0):tLayerCost);
            const ValueType tEnergyModif = vUnaryResegmLUT(nNewLabel)+((nNewLabel==nInitLabel)?cost_cast(0):tLayerCost);
        #else //!SEGMMATCH_CONFIG_USE_TEMPORAL_U_CST
            const ValueType tEnergyCurr = vUnaryResegmLUT(nCurrLabel);
            const ValueType tEnergyModif = vUnaryResegmLUT(nNewLabel);
        #endif //!SEGMMATCH_CONFIG_USE_TEMPORAL_U_CST
            tUnaryCost = tEnergyModif-tEnergyCurr;
        }
        else
            tUnaryCost = cost_cast(0);
    }
}

#if (SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF)

template<typename TNode>
size_t SegmMatcher::GraphModelData::initMinimizer(sospd::SubmodularIBFS<ValueType,IndexType>& oMinimizer,
                                                  const std::vector<TNode>& vNodeMap,
                                                  const std::vector<size_t>& vGraphIdxToMapIdxLUT) {
    lvDbgExceptionWatch;
    const size_t nGraphNodes = vGraphIdxToMapIdxLUT.size();
    oMinimizer.AddNode((int)nGraphNodes);
    size_t nCliqueCount = 0;
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const NodeInfo& oNode = vNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode);
        for(auto& pClique : oNode.vpCliques) {
            lvDbgAssert(pClique);
            const Clique& oClique = *pClique;
            lvDbgAssert(oClique);
            const IndexType nCliqueSize = oClique.getSize();
            lvDbgAssert(nCliqueSize<=s_nMaxOrder);
            const IndexType* aGraphNodeIdxs = oClique.getGraphNodeIter();
            lvDbgAssert(std::find(aGraphNodeIdxs,aGraphNodeIdxs+nCliqueSize,nGraphNodeIdx)<aGraphNodeIdxs+nCliqueSize);
            for(size_t nDimIdx=0; nDimIdx<nCliqueSize; ++nDimIdx)
                lvDbgAssert(aGraphNodeIdxs[nDimIdx]<nGraphNodes);
            oMinimizer.AddClique(
                std::vector<IndexType>(aGraphNodeIdxs,aGraphNodeIdxs+nCliqueSize),
                std::vector<ValueType>(1UL<<nCliqueSize,cost_cast(0))
            );
            ++nCliqueCount;
        }
    }
    return nCliqueCount;
}

template<typename TFunc, typename TNode>
size_t SegmMatcher::GraphModelData::setupPrimalDual(const std::vector<TNode>& vNodeMap,
                                                    const std::vector<size_t>& vGraphIdxToMapIdxLUT,
                                                    const cv::Mat_<InternalLabelType>& oLabeling,
                                                    cv::Mat_<ValueType>& oDualMap,
                                                    cv::Mat_<ValueType>& oHeightMap,
                                                    size_t nTotLabels, size_t nMaxCliques) {
    lvDbgExceptionWatch;
    const size_t nGraphNodes = vGraphIdxToMapIdxLUT.size();
    lvDbgAssert(nGraphNodes>size_t(0) && nMaxCliques>size_t(0));
    lvDbgAssert(nGraphNodes<=oLabeling.total());
    oHeightMap.create((int)nGraphNodes,(int)nTotLabels);
    oHeightMap = cost_cast(0);
    oDualMap.create((int)nMaxCliques,(int)(s_nMaxOrder*nTotLabels));
    oDualMap = cost_cast(0);
    std::array<InternalLabelType,s_nMaxOrder> aLabelingBuffer;
    size_t nCliqueCount = 0;
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const NodeInfo& oNode = vNodeMap[nLUTNodeIdx];
        const ExplicitFunction& vUnaryLUT = *oNode.pUnaryFunc;
        for(size_t nLabelIdx=0; nLabelIdx<nTotLabels; ++nLabelIdx)
            oHeightMap((int)nGraphNodeIdx,(int)nLabelIdx) += vUnaryLUT(nLabelIdx);
        for(auto& pClique : oNode.vpCliques) {
            const Clique& oClique = *pClique;
            const IndexType nCliqueSize = oClique.getSize();
            const IndexType* aLUTNodeIdxs = oClique.getLUTNodeIter();
            for(IndexType nDimIdx=0; nDimIdx<nCliqueSize; ++nDimIdx) {
                const IndexType nOffsetLUTNodeIdx = aLUTNodeIdxs[nDimIdx];
                lvDbgAssert(nOffsetLUTNodeIdx!=SIZE_MAX && nOffsetLUTNodeIdx<oLabeling.total());
                aLabelingBuffer[nDimIdx] = ((InternalLabelType*)oLabeling.data)[nOffsetLUTNodeIdx];
            }
            const TFunc* pvEnergyLUT = (TFunc*)oClique.getFunctionPtr();
            lvDbgAssert(pvEnergyLUT);
            const TFunc& vEnergyLUT = *pvEnergyLUT;
            const ValueType tCurrCost = vEnergyLUT(aLabelingBuffer.data());
            lvDbgAssert(tCurrCost>=cost_cast(0));
            lvDbgAssert(nCliqueCount<nMaxCliques);
            ValueType* pLambdas = oDualMap.ptr<ValueType>((int)nCliqueCount);
            ValueType tAvgCost = tCurrCost/nCliqueSize;
            const int tRemainderCost = int(tCurrCost)%int(nCliqueSize);
            const IndexType* aGraphNodeIdxs = oClique.getGraphNodeIter();
            lvDbgAssert(std::find(aGraphNodeIdxs,aGraphNodeIdxs+nCliqueSize,nGraphNodeIdx)<aGraphNodeIdxs+nCliqueSize);
            for(IndexType nDimIdx=0; nDimIdx<nCliqueSize; ++nDimIdx) {
                const IndexType nOffsetGraphNodeIdx = aGraphNodeIdxs[nDimIdx];
                lvDbgAssert(nOffsetGraphNodeIdx!=SIZE_MAX && nOffsetGraphNodeIdx<nGraphNodes);
                ValueType& tLambda = pLambdas[nDimIdx*nTotLabels+aLabelingBuffer[nDimIdx]];
                tLambda = tAvgCost;
                if(int(nDimIdx)<tRemainderCost)
                    tLambda += cost_cast(1);
                oHeightMap((int)nOffsetGraphNodeIdx,(int)aLabelingBuffer[nDimIdx]) += tLambda;
            }
            ++nCliqueCount;
        }
    }
    return nCliqueCount;
}

template<typename TFunc, typename TNode>
void SegmMatcher::GraphModelData::solvePrimalDual(sospd::SubmodularIBFS<ValueType,IndexType>& oMinimizer,
                                                  const std::vector<TNode>& vNodeMap,
                                                  const std::vector<size_t>& vGraphIdxToMapIdxLUT,
                                                  const cv::Mat_<ValueType>& oUnaryCostMap,
                                                  cv::Mat_<InternalLabelType>& oLabeling,
                                                  cv::Mat_<ValueType>& oDualMap,
                                                  cv::Mat_<ValueType>& oHeightMap,
                                                  InternalLabelType nAlphaLabel,
                                                  size_t nTotLabels,
                                                  bool bUpdateAssocs,
                                                  TemporalArray<CamArray<size_t>>& aanChangedLabels) {
    lvDbgExceptionWatch;
    lvDbgAssert(!oDualMap.empty() && !oHeightMap.empty());
    const size_t nGraphNodes = vGraphIdxToMapIdxLUT.size();
    std::vector<bool>& fixedVars = oMinimizer.Params().fixedVars;
    fixedVars.resize(nGraphNodes);
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
        fixedVars[nGraphNodeIdx] = (((InternalLabelType*)oLabeling.data)[nLUTNodeIdx]==nAlphaLabel);
    }
    std::array<InternalLabelType,s_nMaxOrder> label_buf,current_labels,fusion_labels;
    std::array<ValueType,s_nMaxOrder> current_lambda,fusion_lambda;
    auto& oMinimizer_cliques = oMinimizer.Graph().GetCliques();
    size_t nCliqueCount = 0;
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const NodeInfo& oNode = vNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode);
        for(const Clique* pClique : oNode.vpCliques) {
            lvDbgAssert(pClique && *pClique);
            const Clique& oClique = *pClique;
            const size_t nCliqueSize = oClique.getSize();
            lvDbgAssert(nCliqueSize<=s_nMaxOrder);
            lvDbgAssert(int(nCliqueCount)<oDualMap.rows);
            const ValueType* pLambdas = oDualMap.ptr<ValueType>((int)nCliqueCount);
            auto& oMinimizer_c = oMinimizer_cliques[nCliqueCount];
            lvDbgAssert(nCliqueSize==oMinimizer_c.Size());
            std::vector<ValueType>& energy_table = oMinimizer_c.EnergyTable();
            sospd::Assgn max_assgn = sospd::Assgn(1UL<<nCliqueSize);
            lvDbgAssert(energy_table.size() == max_assgn);
            for(size_t i = 0; i < nCliqueSize; ++i) {
                lvDbgAssert(oClique.getGraphNodeIdx(i)==oMinimizer_c.Nodes()[i]);
                lvDbgAssert(oClique.getLUTNodeIdx(i)==vGraphIdxToMapIdxLUT[oMinimizer_c.Nodes()[i]]);
                current_labels[i] = ((InternalLabelType*)oLabeling.data)[vGraphIdxToMapIdxLUT[oMinimizer_c.Nodes()[i]]];
                fusion_labels[i] = nAlphaLabel;
                current_lambda[i] = pLambdas[i*nTotLabels+current_labels[i]];
                fusion_lambda[i] = pLambdas[i*nTotLabels+fusion_labels[i]];
            }
            const TFunc& vCliqueLUT = *(TFunc*)oClique.getFunctionPtr();
            // compute costs of all fusion assignments
            sospd::Assgn last_gray = 0;
            for(size_t i_idx = 0; i_idx < nCliqueSize; ++i_idx)
                label_buf[i_idx] = current_labels[i_idx];
            energy_table[0] = vCliqueLUT(label_buf.data());
            for(sospd::Assgn a = 1; a < max_assgn; ++a) {
                sospd::Assgn gray = a ^ (a >> 1);
                sospd::Assgn diff = gray ^ last_gray;
                int changed_idx = __builtin_ctz(diff);
                if(diff & gray)
                    label_buf[changed_idx] = fusion_labels[changed_idx];
                else
                    label_buf[changed_idx] = current_labels[changed_idx];
                last_gray = gray;
                energy_table[gray] = vCliqueLUT(label_buf.data());
            }
            // compute the residual function: g(S) - lambda_fusion(S) - lambda_current(C\S)
            sospd::SubtractLinear(nCliqueSize,energy_table,fusion_lambda,current_lambda);
            lvDbgAssert__(energy_table[0] == 0,"%d",energy_table[0]); // check tightness of current labeling
            ++nCliqueCount;
        }
    }
    lvDbgAssert(nCliqueCount==oMinimizer.Graph().GetCliques().size());
    oMinimizer.ClearUnaries();
    oMinimizer.AddConstantTerm(-oMinimizer.GetConstantTerm());
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const InternalLabelType nInitLabel = ((InternalLabelType*)oLabeling.data)[nLUTNodeIdx];
        const NodeInfo& oNode = vNodeMap[nLUTNodeIdx];
        ValueType tUnaryCost = -((ValueType*)oUnaryCostMap.data)[nLUTNodeIdx];
        for(const auto& p : oNode.vCliqueMemberLUT) {
            const size_t nCliqueIdx = p.first;
            lvDbgAssert__((int)nCliqueIdx<oDualMap.rows,"%d",(int)nCliqueIdx);
            const ValueType* pDualRowPtr = oDualMap.ptr<ValueType>((int)nCliqueIdx);
            const size_t nCliqueDimIdx = p.second;
            lvDbgAssert(int(nCliqueDimIdx*nTotLabels+nInitLabel)<oDualMap.cols);
            const ValueType tInitCliqueCost = pDualRowPtr[nCliqueDimIdx*nTotLabels+nInitLabel];
            lvDbgAssert(int(nCliqueDimIdx*nTotLabels+nAlphaLabel)<oDualMap.cols);
            const ValueType tNewCliqueCost = pDualRowPtr[nCliqueDimIdx*nTotLabels+nAlphaLabel];
            tUnaryCost += tInitCliqueCost-tNewCliqueCost;
        }
        if(tUnaryCost>cost_cast(0))
            oMinimizer.AddUnaryTerm((int)nGraphNodeIdx,tUnaryCost,0);
        else
            oMinimizer.AddUnaryTerm((int)nGraphNodeIdx,0,-tUnaryCost);
    }
    oMinimizer.Solve();
    /*cv::Mat_<uchar> oLabelSwapMap;
    const bool bDisplaySwapMap = lv::getVerbosity()>=4;
    if(bDisplaySwapMap) {
        oLabelSwapMap.create(oLabeling.dims,oLabeling.size);
        oLabelSwapMap = 0u;
    }*/
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const int nMoveLabel = oMinimizer.GetLabel((int)nGraphNodeIdx);
        lvDbgAssert(nMoveLabel==0 || nMoveLabel==1 || nMoveLabel<0);
        if(nMoveLabel==1) { // node label changed to alpha
            const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
            InternalLabelType& nLabel = ((InternalLabelType*)oLabeling.data)[nLUTNodeIdx];
            if(nLabel!=nAlphaLabel) {
                const NodeInfo& oNode = vNodeMap[nLUTNodeIdx];
                if(bUpdateAssocs && nLabel<m_nDontCareLabelIdx)
                    removeAssoc(oNode.nRowIdx,oNode.nColIdx,nLabel);
                nLabel = nAlphaLabel;
                if(bUpdateAssocs && nAlphaLabel<m_nDontCareLabelIdx)
                    addAssoc(oNode.nRowIdx,oNode.nColIdx,nAlphaLabel);
                ++aanChangedLabels[oNode.nLayerIdx][oNode.nCamIdx];
                /*if(bDisplaySwapMap)
                    oLabelSwapMap.data[nLUTNodeIdx] = UCHAR_MAX;*/
            }
        }
    }
    /*if(bDisplaySwapMap) {
        cv::imshow("curr-swaps",oLabelSwapMap);
        cv::waitKey(1);
    }*/
    for(size_t nCliqueIdx=0; nCliqueIdx<nCliqueCount; ++nCliqueIdx) {
        auto& oMinimizer_c = oMinimizer_cliques[nCliqueIdx];
        const std::vector<ValueType>& phiCi = oMinimizer_c.AlphaCi();
        for (size_t j = 0; j < phiCi.size(); ++j) {
            oDualMap((int)nCliqueIdx,(int)(j*nTotLabels+nAlphaLabel)) += phiCi[j];
            oHeightMap((int)oMinimizer_c.Nodes()[j],(int)nAlphaLabel) += phiCi[j];
        }
    }
    for(size_t nGraphNodeIdx=0,nCliqueIdx=0; nGraphNodeIdx<nGraphNodes; ++nGraphNodeIdx) {
        const size_t nLUTNodeIdx = vGraphIdxToMapIdxLUT[nGraphNodeIdx];
        const NodeInfo& oNode = vNodeMap[nLUTNodeIdx];
        lvDbgAssert(oNode.bValidGraphNode);
        for(const Clique* pClique : oNode.vpCliques) {
            lvDbgAssert(pClique && *pClique);
            const Clique& oClique = *pClique;
            const size_t nCliqueSize = oClique.getSize();
            auto& oMinimizer_c = oMinimizer_cliques[nCliqueIdx];
            ValueType lambdaSum = 0;
            for(size_t nDimIdx=0; nDimIdx<nCliqueSize; ++nDimIdx) {
                lvDbgAssert(oMinimizer_c.Nodes()[nDimIdx]==oClique.getGraphNodeIdx(nDimIdx));
                lvDbgAssert(vGraphIdxToMapIdxLUT[oMinimizer_c.Nodes()[nDimIdx]]==oClique.getLUTNodeIdx(nDimIdx));
                label_buf[nDimIdx] = ((InternalLabelType*)oLabeling.data)[vGraphIdxToMapIdxLUT[oMinimizer_c.Nodes()[nDimIdx]]];
                lambdaSum += oDualMap((int)nCliqueIdx,int(nDimIdx*nTotLabels+label_buf[nDimIdx]));
            }
            const TFunc& vCliqueLUT = *(TFunc*)oClique.getFunctionPtr();
            const ValueType energy = vCliqueLUT(label_buf.data());
            const ValueType correction = energy - lambdaSum;
            lvDbgAssert__(correction<=0,"bad clique in post edit dual; id=%d, corr=%d, energy=%d, lambdasum=%d",(int)nCliqueIdx,(int)correction,(int)energy,(int)lambdaSum);
            ValueType avg = correction / cost_cast(nCliqueSize);
            ValueType remainder = correction % cost_cast(nCliqueSize);
            if(remainder<0) {
                avg -= 1;
                remainder += cost_cast(nCliqueSize);
            }
            for(size_t nDimIdx=0; nDimIdx<nCliqueSize; ++nDimIdx) {
                ValueType& lambda_ail = oDualMap(nCliqueIdx,int(nDimIdx*nTotLabels+label_buf[nDimIdx]));
                lvDbgAssert(oMinimizer_c.Nodes()[nDimIdx]==oClique.getGraphNodeIdx(nDimIdx));
                oHeightMap((int)oMinimizer_c.Nodes()[nDimIdx],(int)label_buf[nDimIdx]) -= lambda_ail;
                lambda_ail += avg;
                if((int)nDimIdx<remainder)
                    lambda_ail += 1;
                oHeightMap((int)oMinimizer_c.Nodes()[nDimIdx],(int)label_buf[nDimIdx]) += lambda_ail;
            }
            ++nCliqueIdx;
        }
    }
}

#endif //(SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF || SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF)

opengm::InferenceTermination SegmMatcher::GraphModelData::infer() {
    static_assert(s_nInputArraySize==4 && getCameraCount()==2,"hardcoded indices below will break");
    lvDbgExceptionWatch;
    const size_t nCameraCount = getCameraCount();
    if(lv::getVerbosity()>=4) {
        cv::Mat oTargetImg = m_aaInputs[0][m_nPrimaryCamIdx*InputPackOffset+InputPackOffset_Img].clone();
        if(oTargetImg.channels()==1)
            cv::cvtColor(oTargetImg,oTargetImg,cv::COLOR_GRAY2BGR);
        cv::Mat oTargetMask = m_aaInputs[0][m_nPrimaryCamIdx*InputPackOffset+InputPackOffset_Mask].clone();
        cv::cvtColor(oTargetMask,oTargetMask,cv::COLOR_GRAY2BGR);
        oTargetMask &= cv::Vec3b(255,0,0);
        cv::imshow("primary input",(oTargetImg+oTargetMask)/2);
        if(nCameraCount==size_t(2)) {
            const size_t nSecondaryCamIdx = m_nPrimaryCamIdx^1;
            cv::Mat oOtherImg = m_aaInputs[0][(nSecondaryCamIdx)*InputPackOffset+InputPackOffset_Img].clone();
            if(oOtherImg.channels()==1)
                cv::cvtColor(oOtherImg,oOtherImg,cv::COLOR_GRAY2BGR);
            cv::Mat oOtherMask = m_aaInputs[0][(nSecondaryCamIdx)*InputPackOffset+InputPackOffset_Mask].clone();
            cv::cvtColor(oOtherMask,oOtherMask,cv::COLOR_GRAY2BGR);
            oOtherMask &= cv::Vec3b(255,0,0);
            cv::imshow("other input",(oOtherImg+oOtherMask)/2);
        }
        cv::waitKey(1);
    }
    const size_t nTemporalLayerCount = getTemporalLayerCount();
    // we only overwrite current stereo labeling temporal layer, others have been kept & shifted in 'apply'
    updateStereoModel(true); // quick-init for wta disparity map & offset cam estimation
    resetStereoLabelings();
    computeMedianLabelings();
    computeOcclusionMaps();
    updateStereoModel(false); // second init allows proper usage of shape and occlusion maps in model
    resetStereoLabelings();
    lvDbgAssert(m_nValidResegmGraphNodes==m_vResegmGraphIdxToMapIdxLUT.size());
    lvLog_(2,"Running inference for primary camera idx=%d...",(int)m_nPrimaryCamIdx);
#if (SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF || SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF)
    using HOEReducer = HigherOrderEnergy<ValueType,s_nMaxOrder>;
#endif //(SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF || SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF)
    cv::Mat_<InternalLabelType>& oCurrStereoLabeling = m_aaStereoLabelings[0][m_nPrimaryCamIdx];
#if SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
    //calcStereoCosts(m_nPrimaryCamIdx);
    //const size_t nStereoPairwCount = m_nValidStereoGraphNodes*s_nPairwOrients; (max, not actual) // numPairs_
    // pairs_ : lookup node, 1st pairw node idx + 2nd pairw node idx
    // distance_[2ndlabel*nlabels + 1stlabel] : m_aaStereoPairwFuncIDs_base[nCamIdx][nOrientIdx](1stlabel,2ndlabel)
    // weights_ : lookup node, each orient has its weight
    /* still need to cleanup fastpd setup...
    setLabelCosts();
    getNumPairs();
    setPairs();
    setDistance();
    setWeights();
    pdInference_ = new fastPDLib::CV_Fast_PD(
            gm_.numberOfVariables(),
            gm_.numberOfLabels(0),
            labelCosts_,
            numPairs_,
            pairs_,
            distance_,
            parameter_.numberOfIterations_,
            weights_
    );*/
    // see if maxflow used in fastpd can be replaced by https://github.com/gerddie/maxflow?
#elif SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF
    constexpr int nMaxStereoEdgesPerNode = (s_nPairwOrients+s_nEpipolarCliqueEdges);
    kolmogorov::qpbo::QPBO<ValueType> oStereoMinimizer((int)m_nValidStereoGraphNodes,(int)m_nValidStereoGraphNodes*nMaxStereoEdgesPerNode);
    HOEReducer oStereoReducer;
    size_t nStereoLabelOrderingIdx = 0;
#elif SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF
    static_assert(std::is_integral<SegmMatcher::ValueType>::value,"sospd height weight redistr requires integer type");
    constexpr bool bUseHeightAlphaExp = SEGMMATCH_CONFIG_USE_SOSPD_ALPHA_HEIGHTS_LABEL_ORDERING;
    lvAssert_(!bUseHeightAlphaExp,"missing impl");
    size_t nStereoLabelOrderingIdx = 0;
    sospd::SubmodularIBFS<ValueType,IndexType> oStereoMinimizer;
    const size_t nInternalStereoCliqueCount = initMinimizer(oStereoMinimizer,m_vStereoNodeMap,m_vStereoGraphIdxToMapIdxLUT);
    lvAssert(nInternalStereoCliqueCount==m_nStereoCliqueCount);
    const size_t nSetupStereoCliqueCount = setupPrimalDual<ExplicitScaledFunction>(m_vStereoNodeMap,m_vStereoGraphIdxToMapIdxLUT,oCurrStereoLabeling,m_oStereoDualMap,m_oStereoHeightMap,m_nStereoLabels,m_nStereoCliqueCount);
    lvAssert(nSetupStereoCliqueCount==m_nStereoCliqueCount);
#endif //SEGMMATCH_CONFIG_USE_..._STEREO_INF
#if SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF
    constexpr int nMaxResegmEdgesPerNode = (s_nPairwOrients+s_nTemporalCliqueEdges);
    kolmogorov::qpbo::QPBO<ValueType> oResegmMinimizer((int)m_nValidResegmGraphNodes,(int)m_nValidResegmGraphNodes*nMaxResegmEdgesPerNode);
    HOEReducer oResegmReducer;
#elif SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF
    static_assert(std::is_integral<SegmMatcher::ValueType>::value,"sospd height weight redistr requires integer type");
#endif //SEGMMATCH_CONFIG_USE_..._RESEGM_INF
    size_t nStereoMoveIter=0, nResegmMoveIter=0, nConsecUnchangedStereoLabels=0;
    std::vector<int> vInitLabelCounts = lv::calcHistCounts(m_aaStereoLabelings[0][m_nPrimaryCamIdx],m_aROIs[m_nPrimaryCamIdx]);
    vInitLabelCounts.resize(m_nStereoLabels);
    m_vStereoLabelOrdering = lv::sort_indices<InternalLabelType>(vInitLabelCounts,[&](size_t a, size_t b) {
        return ((a==m_nDontCareLabelIdx)?true:(b==m_nDontCareLabelIdx)?false:(vInitLabelCounts[a]>vInitLabelCounts[b]));
    });
    lvDbgAssert(!m_vStereoLabelOrdering.empty() && m_vStereoLabelOrdering[0]==m_nDontCareLabelIdx);
    lvDbgAssert(lv::unique(m_vStereoLabelOrdering.begin(),m_vStereoLabelOrdering.end())==lv::make_range(InternalLabelType(0),InternalLabelType(m_nStereoLabels-1)));
    // note: sospd might not follow this label order if using alpha heights strategy (reimpl to use same strat in every solver?) ####
    lv::StopWatch oLocalTimer;
    ValueType tLastStereoEnergy=m_pStereoInf->value(),tLastResegmEnergy=std::numeric_limits<ValueType>::max();
    m_oSuperStackedResegmLabeling.copyTo(m_oInitSuperStackedResegmLabeling);
    cv::Mat_<InternalLabelType> oPreStereoUpdateLabeling = m_oSuperStackedResegmLabeling.clone();
    cv::Mat_<InternalLabelType> oPreResegmUpdateLabeling = m_oSuperStackedResegmLabeling.clone();
    bool bJustUpdatedSegm = false;
    while(nStereoMoveIter<m_nMaxStereoMoveCount && nConsecUnchangedStereoLabels<m_nStereoLabels) {
    #if SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF

        // fastpd only works with shared+scaled pairwise costs, and no higher order terms
        opengm::external::FastPD<StereoModelType> oStereoMinimizer2(*m_apStereoModels[nPrimaryCamIdx],opengm::external::FastPD<StereoModelType>::Parameter());
        oStereoMinimizer2.infer();
        std::vector<InternalLabelType> outputlabels;
        oStereoMinimizer2.arg(outputlabels);
        lvAssert(outputlabels.size()==m_anValidGraphNodes[nPrimaryCamIdx]);

        //pdInference_->run(); just one iter at a time --- split and use while above; add nullify stereo pairw costs?

        size_t nChangedStereoLabels = 0;
        for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_anValidGraphNodes[nPrimaryCamIdx]; ++nGraphNodeIdx) {
            const size_t nLUTNodeIdx = m_avValidLUTNodeIdxs[nPrimaryCamIdx][nGraphNodeIdx];
            const int nRowIdx = m_vNodeInfos[nLUTNodeIdx].nRowIdx;
            const int nColIdx = m_vNodeInfos[nLUTNodeIdx].nColIdx;
            const InternalLabelType nOldLabel = oCurrStereoLabeling(nRowIdx,nColIdx);
            //const InternalLabelType nNewLabel = pdInference_->_pinfo[nGraphNodeIdx].label;
            const InternalLabelType nNewLabel = outputlabels[nGraphNodeIdx];
            if(nOldLabel!=nNewLabel) {
                if(nOldLabel<m_nDontCareLabelIdx)
                    removeAssoc(nPrimaryCamIdx,nRowIdx,nColIdx,nOldLabel);
                oCurrStereoLabeling(nRowIdx,nColIdx) = nNewLabel;
                if(nNewLabel<m_nDontCareLabelIdx)
                    addAssoc(nPrimaryCamIdx,nRowIdx,nColIdx,nNewLabel);
                ++nChangedStereoLabels;
            }
        }
        nStereoMoveIter += m_nRealStereoLabels;
        nConsecUnchangedStereoLabels = (nChangedStereoLabels>0)?0:nConsecUnchangedStereoLabels+m_nRealStereoLabels;
        const bool bResegmNext = true;
    #elif SEGMMATCH_CONFIG_USE_FGBZ_STEREO_INF
        // each iter below is a fusion move based on A. Fix's energy minimization method for higher-order MRFs
        // see "A Graph Cut Algorithm for Higher-order Markov Random Fields" in ICCV2011 for more info (doi = 10.1109/ICCV.2011.6126347)
        // (note: this approach is very generic, and not very well adapted to a dynamic MRF problem!)
        const InternalLabelType nStereoAlphaLabel = m_vStereoLabelOrdering[nStereoLabelOrderingIdx];
        calcStereoMoveCosts(nStereoAlphaLabel);
        oStereoReducer.Clear();
        oStereoReducer.AddVars((int)m_nValidStereoGraphNodes);
        if(lv::getVerbosity()>=5) {
            cv::Mat oStereoUnaryCostsDisplay;
            cv::normalize(m_oStereoUnaryCosts,oStereoUnaryCostsDisplay,255,0,cv::NORM_MINMAX,CV_8U,m_aROIs[m_nPrimaryCamIdx]);
            cv::imshow("oStereoUnaryCostsDisplay",oStereoUnaryCostsDisplay);
            cv::waitKey(1);
        }
        for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
            const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
            const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
            if(oNode.nUnaryFactID!=SIZE_MAX) {
                const ValueType& tUnaryCost = ((ValueType*)m_oStereoUnaryCosts.data)[nLUTNodeIdx];
                lvDbgAssert(&tUnaryCost==&m_oStereoUnaryCosts(oNode.nRowIdx,oNode.nColIdx));
                oStereoReducer.AddUnaryTerm((int)nGraphNodeIdx,tUnaryCost);
            }
            for(size_t nOrientIdx=0; nOrientIdx<s_nPairwOrients; ++nOrientIdx)
                lv::gm::factorReducer<ExplicitScaledFunction>(oNode.aPairwCliques[nOrientIdx],oStereoReducer,nStereoAlphaLabel,(InternalLabelType*)oCurrStereoLabeling.data);
        #if SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN
            lv::gm::factorReducer<ExplicitScaledFunction>(oNode.oEpipolarClique,oStereoReducer,nStereoAlphaLabel,(InternalLabelType*)oCurrStereoLabeling.data);
        #endif //SEGMMATCH_CONFIG_USE_EPIPOLAR_CONN
        }
        oStereoMinimizer.Reset();
        oStereoReducer.ToQuadratic(oStereoMinimizer);
        oStereoMinimizer.Solve();
        oStereoMinimizer.ComputeWeakPersistencies();
        size_t nChangedStereoLabels = 0;
        for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidStereoGraphNodes; ++nGraphNodeIdx) {
            const size_t nLUTNodeIdx = m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
            const int nRowIdx = m_vStereoNodeMap[nLUTNodeIdx].nRowIdx;
            const int nColIdx = m_vStereoNodeMap[nLUTNodeIdx].nColIdx;
            const int nMoveLabel = oStereoMinimizer.GetLabel((int)nGraphNodeIdx);
            lvDbgAssert(nMoveLabel==0 || nMoveLabel==1 || nMoveLabel<0);
            if(nMoveLabel==1) { // node label changed to alpha
                const InternalLabelType nOldLabel = oCurrStereoLabeling(nRowIdx,nColIdx);
                if(nOldLabel<m_nDontCareLabelIdx)
                    removeAssoc(nRowIdx,nColIdx,nOldLabel);
                oCurrStereoLabeling(nRowIdx,nColIdx) = nStereoAlphaLabel;
                if(nStereoAlphaLabel<m_nDontCareLabelIdx)
                    addAssoc(nRowIdx,nColIdx,nStereoAlphaLabel);
                ++nChangedStereoLabels;
            }
        }
        ++nStereoLabelOrderingIdx %= m_vStereoLabelOrdering.size();
        nConsecUnchangedStereoLabels = (nChangedStereoLabels>0)?0:nConsecUnchangedStereoLabels+1;
        const bool bResegmNext = (nStereoMoveIter++%SEGMMATCH_DEFAULT_ITER_PER_RESEGM)==0;
    #elif SEGMMATCH_CONFIG_USE_SOSPD_STEREO_INF
        const InternalLabelType nStereoAlphaLabel = m_vStereoLabelOrdering[nStereoLabelOrderingIdx];
        calcStereoMoveCosts(nStereoAlphaLabel);
        const bool bStereoMoveCanFlipLabels = std::any_of(StereoGraphNodeIter(this,0),StereoGraphNodeIter(this,m_nValidStereoGraphNodes),[&](const StereoNodeInfo& oNode) {
            return (((InternalLabelType*)oCurrStereoLabeling.data)[oNode.nMapIdx])!=nStereoAlphaLabel;
        });
        TemporalArray<CamArray<size_t>> aanChangedStereoLabels{};
        if(bStereoMoveCanFlipLabels)
            solvePrimalDual<ExplicitScaledFunction>(oStereoMinimizer,
                                                    m_vStereoNodeMap,
                                                    m_vStereoGraphIdxToMapIdxLUT,
                                                    m_oStereoUnaryCosts,
                                                    oCurrStereoLabeling,
                                                    m_oStereoDualMap,
                                                    m_oStereoHeightMap,
                                                    nStereoAlphaLabel,
                                                    m_nStereoLabels,true,
                                                    aanChangedStereoLabels);
        const bool bGotStereoLabelChange = aanChangedStereoLabels[0][m_nPrimaryCamIdx]>0;
        ++nStereoLabelOrderingIdx %= m_vStereoLabelOrdering.size();
        nConsecUnchangedStereoLabels = bGotStereoLabelChange?0:nConsecUnchangedStereoLabels+1;
        const bool bResegmNext = (nStereoMoveIter++%SEGMMATCH_DEFAULT_ITER_PER_RESEGM)==0;
    #endif //SEGMMATCH_CONFIG_USE_..._STEREO_INF
        if(lv::getVerbosity()>=3) {
            cv::Mat oCurrLabelingDisplay = getStereoDispMapDisplay(0,m_nPrimaryCamIdx);
            if(oCurrLabelingDisplay.size().area()<640*480)
                cv::resize(oCurrLabelingDisplay,oCurrLabelingDisplay,cv::Size(),2,2,cv::INTER_NEAREST);
            cv::imshow(std::string("disp-")+std::to_string(m_nPrimaryCamIdx),oCurrLabelingDisplay);
            cv::waitKey(1);
        }
        const ValueType tCurrStereoEnergy = m_pStereoInf->value();
        lvDbgAssert(tCurrStereoEnergy>=cost_cast(0));
        std::stringstream ssStereoEnergyDiff;
        if((tCurrStereoEnergy-tLastStereoEnergy)==cost_cast(0))
            ssStereoEnergyDiff << "null";
        else
            ssStereoEnergyDiff << std::showpos << tCurrStereoEnergy-tLastStereoEnergy;
    #if SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
        // no control on label w/ fastpd (could decompose algo later on...)
        lvLog_(2,"\t\tdisp      e = %d      (delta=%s)      [stereo-iter=%d]",(int)tCurrStereoEnergy,ssStereoEnergyDiff.str().c_str(),(int)nStereoMoveIter);
    #else //!SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
        lvLog_(2,"\t\tdisp [+label:%d]   e = %d   (delta=%s)      [stereo-iter=%d]",(int)nStereoAlphaLabel,(int)tCurrStereoEnergy,ssStereoEnergyDiff.str().c_str(),(int)nStereoMoveIter);
    #endif //!SEGMMATCH_CONFIG_USE_FASTPD_STEREO_INF
        if(bJustUpdatedSegm) // if segmentation changes, stereo priors change, thus energy can spike up
            lvLog(4,"\t\t\t(just updated segmentation)");
        else
            lvAssert_(tLastStereoEnergy>=tCurrStereoEnergy,"stereo energy not minimizing!");
        tLastStereoEnergy = tCurrStereoEnergy;
        bJustUpdatedSegm = false;
        if(bResegmNext) {
            lvLog(4,"init resegm pass...");
            for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
                if(nCamIdx!=m_nPrimaryCamIdx)
                    resetStereoLabelingByProjection(nCamIdx);
            }
            if(m_nFramesProcessed==0u) { // copy latest stereo labeling to all temporal layers if initial frame
                for(size_t nLayerIdx=1u; nLayerIdx<getTemporalLayerCount(); ++nLayerIdx)
                    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx)
                        m_aaStereoLabelings[0][nCamIdx].copyTo(m_aaStereoLabelings[nLayerIdx][nCamIdx]);
            }
            size_t nTotChangedResegmLabels=0,nConsecUnchangedResegmLabels=0;
            constexpr std::array<InternalLabelType,2> anResegmLabels = {s_nForegroundLabelIdx,s_nBackgroundLabelIdx};
            const size_t nInitResegmMoveIter = nResegmMoveIter;
        #if SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF
            sospd::SubmodularIBFS<ValueType,IndexType> oResegmMinimizer;
            size_t nInternalResegmCliqueCount = 0;
        #endif //SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF
            TemporalArray<CamArray<size_t>> aanChangedResegmLabels{};
            while((++nResegmMoveIter-nInitResegmMoveIter)<=m_nMaxResegmMoveCount && nConsecUnchangedResegmLabels<s_nResegmLabels) {
                const bool bInitResegmIter = (nResegmMoveIter-nInitResegmMoveIter)==1u;
                const bool bNewResegmIter = ((nResegmMoveIter-nInitResegmMoveIter)%s_nResegmLabels)==1u;
                const InternalLabelType nResegmAlphaLabel = anResegmLabels[nResegmMoveIter%s_nResegmLabels];
                if(bNewResegmIter || SEGMMATCH_CONFIG_USE_CONT_RESEGM_UPDT) {
                    for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx)
                        for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx)
                            if(aanChangedResegmLabels[nLayerIdx][nCamIdx]>0u)
                                calcShapeDistFeatures(m_aaResegmLabelings[nLayerIdx][nCamIdx],nCamIdx,m_avFeatures[nLayerIdx]);
                    aanChangedResegmLabels = TemporalArray<CamArray<size_t>>{};
                    updateResegmModel(bInitResegmIter);
                    if(bNewResegmIter)
                        m_oSuperStackedResegmLabeling.copyTo(oPreResegmUpdateLabeling);
                }
            #if SEGMMATCH_CONFIG_USE_FGBZ_RESEGM_INF
                calcResegmMoveCosts(nResegmAlphaLabel);
                oResegmReducer.Clear();
                oResegmReducer.AddVars((int)m_nValidResegmGraphNodes);
                for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
                    const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
                    const ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
                    if(oNode.nUnaryFactID!=SIZE_MAX) {
                        const ValueType& tUnaryCost = ((ValueType*)m_oResegmUnaryCosts.data)[nLUTNodeIdx];
                        lvDbgAssert(&tUnaryCost==&m_oResegmUnaryCosts(oNode.nRowIdx+int((oNode.nCamIdx*nTemporalLayerCount+oNode.nLayerIdx)*m_oGridSize[0]),oNode.nColIdx));
                        oResegmReducer.AddUnaryTerm((int)nGraphNodeIdx,tUnaryCost);
                    }
                    for(size_t nOrientIdx=0; nOrientIdx<s_nPairwOrients; ++nOrientIdx)
                        lv::gm::factorReducer<ExplicitFunction>(oNode.aPairwCliques[nOrientIdx],oResegmReducer,nResegmAlphaLabel,(InternalLabelType*)m_oSuperStackedResegmLabeling.data);
                #if SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
                    lv::gm::factorReducer<ExplicitFunction>(oNode.oTemporalClique,oResegmReducer,nResegmAlphaLabel,(InternalLabelType*)m_oSuperStackedResegmLabeling.data);
                #endif //SEGMMATCH_CONFIG_USE_TEMPORAL_CONN
                }
                oResegmMinimizer.Reset();
                oResegmReducer.ToQuadratic(oResegmMinimizer);
                oResegmMinimizer.Solve();
                oResegmMinimizer.ComputeWeakPersistencies();
                for(size_t nGraphNodeIdx=0; nGraphNodeIdx<m_nValidResegmGraphNodes; ++nGraphNodeIdx) {
                    const size_t nLUTNodeIdx = m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
                    const ResegmNodeInfo& oNode = m_vResegmNodeMap[nLUTNodeIdx];
                    const int nMoveLabel = oResegmMinimizer.GetLabel((int)nGraphNodeIdx);
                    lvDbgAssert(nMoveLabel==0 || nMoveLabel==1 || nMoveLabel<0);
                    if(nMoveLabel==1) { // node label changed to alpha
                        ((InternalLabelType*)m_oSuperStackedResegmLabeling.data)[nLUTNodeIdx] = nResegmAlphaLabel;
                        ++aanChangedResegmLabels[oNode.nLayerIdx][oNode.nCamIdx];
                    }
                }
            #elif SEGMMATCH_CONFIG_USE_SOSPD_RESEGM_INF
                if(bNewResegmIter || SEGMMATCH_CONFIG_USE_CONT_RESEGM_UPDT) {
                    if(bInitResegmIter) { // on the very first iteration, init minimizer with updated clique count
                        nInternalResegmCliqueCount = initMinimizer(oResegmMinimizer,m_vResegmNodeMap,m_vResegmGraphIdxToMapIdxLUT);
                        lvDbgAssert(nInternalResegmCliqueCount<=m_nResegmCliqueCount);
                    }
                    const size_t nSetupResegmCliqueCount = setupPrimalDual<ExplicitFunction>(m_vResegmNodeMap,m_vResegmGraphIdxToMapIdxLUT,m_oSuperStackedResegmLabeling,m_oResegmDualMap,m_oResegmHeightMap,s_nResegmLabels,m_nResegmCliqueCount);
                    lvAssert(nSetupResegmCliqueCount==nInternalResegmCliqueCount && nSetupResegmCliqueCount<=m_nResegmCliqueCount);
                }
                calcResegmMoveCosts(nResegmAlphaLabel);
                const bool bResegmMoveCanFlipLabels = std::any_of(ResegmGraphNodeIter(this,0),ResegmGraphNodeIter(this,m_nValidResegmGraphNodes),[&](const ResegmNodeInfo& oNode) {
                    return (((InternalLabelType*)m_oSuperStackedResegmLabeling.data)[oNode.nLUTIdx])!=nResegmAlphaLabel;
                });
                //cv::Mat costtest;
                //m_oResegmUnaryCosts.convertTo(costtest,CV_32F);
                //cv::normalize(costtest,costtest,1,0,cv::NORM_MINMAX,-1,m_oSuperStackedROI);
                //cv::resize(costtest,costtest,cv::Size(),0.25,0.25);
                //cv::imshow("costtest",costtest);
                //cv::waitKey(1);
                if(bResegmMoveCanFlipLabels)
                    solvePrimalDual<ExplicitFunction>(oResegmMinimizer,
                                                      m_vResegmNodeMap,
                                                      m_vResegmGraphIdxToMapIdxLUT,
                                                      m_oResegmUnaryCosts,
                                                      m_oSuperStackedResegmLabeling,
                                                      m_oResegmDualMap,
                                                      m_oResegmHeightMap,
                                                      nResegmAlphaLabel,
                                                      s_nResegmLabels,false,
                                                      aanChangedResegmLabels);
            #endif //SEGMMATCH_CONFIG_USE_..._RESEGM_INF
                const ValueType tCurrResegmEnergy = m_pResegmInf->value();
                lvDbgAssert(tCurrResegmEnergy>=cost_cast(0));
                std::stringstream ssResegmEnergyDiff;
                if((tCurrResegmEnergy-tLastResegmEnergy)==cost_cast(0))
                    ssResegmEnergyDiff << "null";
                else
                    ssResegmEnergyDiff << std::showpos << tCurrResegmEnergy-tLastResegmEnergy;
                lvLog_(2,"\t\tsegm [+%s]   e = %d   (delta=%s)      [resegm-iter=%d]",(nResegmAlphaLabel==s_nForegroundLabelIdx?"fg":"bg"),(int)tCurrResegmEnergy,ssResegmEnergyDiff.str().c_str(),(int)nResegmMoveIter);
                // note: resegm energy cannot be strictly minimized every iteration since segmentation priors continually change in the loop (it should however stabilize over time)
                tLastResegmEnergy = tCurrResegmEnergy;
                if(lv::getVerbosity()>=3) {
                    for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx) {
                        if(m_nFramesProcessed>=nLayerIdx) {
                            for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
                                cv::Mat oCurrLabelingDisplay = getResegmMapDisplay(nLayerIdx,nCamIdx);
                                if(oCurrLabelingDisplay.size().area()<640*480)
                                    cv::resize(oCurrLabelingDisplay,oCurrLabelingDisplay,cv::Size(),2,2,cv::INTER_NEAREST);
                                cv::imshow(std::string("segm-")+std::to_string(nCamIdx)+" @ t="+std::to_string(nLayerIdx),oCurrLabelingDisplay);
                                cv::waitKey(1);
                            }
                        }
                    }
                }
                if(!bNewResegmIter) {
                    size_t nChangedResegmLabels = 0;
                    for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx)
                        for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx)
                            nChangedResegmLabels += aanChangedResegmLabels[nLayerIdx][nCamIdx];
                    nConsecUnchangedResegmLabels = (nChangedResegmLabels>0u)?0u:nConsecUnchangedResegmLabels+s_nResegmLabels;
                    nTotChangedResegmLabels += nChangedResegmLabels;
                    const double dResegmIterChangeFraction = ((double)cv::countNonZero(oPreResegmUpdateLabeling^m_oSuperStackedResegmLabeling))/m_oSuperStackedResegmLabeling.total();
                    //lvPrint(dResegmIterChangeFraction);
                    if(dResegmIterChangeFraction<1e-4) {
                        lvLog(2,"\t\tsegm early break, local minima detected");
                        break;
                    }
                }
            }
            if(nTotChangedResegmLabels) {
                calcShapeFeatures(m_aaResegmLabelings[0],m_avFeatures[0]); // only need to update latest labeling set for stereo
            #if SEGMMATCH_CONFIG_USE_FULL_DISP_RESETS
                updateStereoModel(false);
                resetStereoLabelings();
            #else //!SEGMMATCH_CONFIG_USE_FULL_DISP_RESETS
                resetStereoLabelingByProjection(m_nPrimaryCamIdx^1u);
            #endif //!SEGMMATCH_CONFIG_USE_FULL_DISP_RESETS
                computeMedianLabelings();
                computeOcclusionMaps();
                updateStereoModel(false);
            #if SEGMMATCH_CONFIG_USE_FULL_DISP_RESETS
                resetStereoLabelings();
            #endif //SEGMMATCH_CONFIG_USE_FULL_DISP_RESETS
                bJustUpdatedSegm = true;
                nConsecUnchangedStereoLabels = 0;
            }
            const double dStereoIterChangeFraction = ((double)cv::countNonZero(oPreStereoUpdateLabeling^m_oSuperStackedResegmLabeling))/m_oSuperStackedResegmLabeling.total();
            //lvPrint(dStereoIterChangeFraction);
            if(dStereoIterChangeFraction<1e-3) {
                lvLog(2,"\t\tstereo early break, local minima detected");
                break;
            }
            m_oSuperStackedResegmLabeling.copyTo(oPreStereoUpdateLabeling);
        }
    }
    // post-proc; update offset stereo labeling to latest proj result, and magic blur all the things (better smoothing than 4-cc!)
    for(size_t nCamIdx=0; nCamIdx<nCameraCount; ++nCamIdx) {
        if(nCamIdx!=m_nPrimaryCamIdx)
            resetStereoLabelingByProjection(nCamIdx); // already smoothed internally
        else
            cv::medianBlur(m_aaStereoLabelings[0][nCamIdx],m_aaStereoLabelings[0][nCamIdx],5);
        for(size_t nLayerIdx=0; nLayerIdx<nTemporalLayerCount; ++nLayerIdx)
            cv::medianBlur(m_aaResegmLabelings[nLayerIdx][nCamIdx],m_aaResegmLabelings[nLayerIdx][nCamIdx],5);
    }
    lvLog_(2,"Inference for primary camera idx=%d completed in %f second(s).",(int)m_nPrimaryCamIdx,oLocalTimer.tock());
    if(lv::getVerbosity()>=4)
        cv::waitKey(0);
    return opengm::InferenceTermination::NORMAL;
}

cv::Mat SegmMatcher::GraphModelData::getResegmMapDisplay(size_t nLayerIdx, size_t nCamIdx) const {
    lvDbgExceptionWatch;
    lvAssert_(nLayerIdx<getTemporalLayerCount(),"layer index out of range");
    lvAssert_(nCamIdx<getCameraCount(),"camera index out of range");
    lvAssert(!m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Img].empty());
    lvAssert(!m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Mask].empty());
    lvAssert(m_oGridSize==m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Img].size);
    lvAssert(m_oGridSize==m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Mask].size);
    lvAssert(!m_aaResegmLabelings[nLayerIdx][nCamIdx].empty() && m_oGridSize==m_aaResegmLabelings[nLayerIdx][nCamIdx].size);
    cv::Mat oOutput(m_oGridSize,CV_8UC3);
    for(int nRowIdx=0; nRowIdx<m_aaResegmLabelings[nLayerIdx][nCamIdx].rows; ++nRowIdx) {
        for(int nColIdx=0; nColIdx<m_aaResegmLabelings[nLayerIdx][nCamIdx].cols; ++nColIdx) {
            const InternalLabelType nCurrLabel = m_aaResegmLabelings[nLayerIdx][nCamIdx](nRowIdx,nColIdx);
            const uchar nInitLabel = m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Mask].at<uchar>(nRowIdx,nColIdx);
            if(nCurrLabel==s_nForegroundLabelIdx && nInitLabel>0u)
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(127,127,127);
            else if(nCurrLabel==s_nBackgroundLabelIdx && nInitLabel>0u)
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(0,0,127);
            else if(nCurrLabel==s_nForegroundLabelIdx && nInitLabel==0u)
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(0,127,0);
            else
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(0,0,0);
        }
    }
    cv::Mat oInputDisplay = m_aaInputs[nLayerIdx][nCamIdx*InputPackOffset+InputPackOffset_Img].clone();
    if(oInputDisplay.channels()==1)
        cv::cvtColor(oInputDisplay,oInputDisplay,cv::COLOR_GRAY2BGR);
    oOutput = (oOutput+oInputDisplay)/2;
    return oOutput;
}

cv::Mat SegmMatcher::GraphModelData::getStereoDispMapDisplay(size_t nLayerIdx, size_t nCamIdx) const {
    lvDbgExceptionWatch;
    lvAssert_(nLayerIdx<getTemporalLayerCount(),"layer index out of range");
    lvAssert_(nCamIdx<getCameraCount(),"camera index out of range");
    lvAssert(m_nMaxDispOffset>m_nMinDispOffset);
    lvAssert(!m_aaStereoLabelings[nLayerIdx][nCamIdx].empty() && m_oGridSize==m_aaStereoLabelings[nLayerIdx][nCamIdx].size);
    const float fRescaleFact = float(UCHAR_MAX)/(m_nMaxDispOffset-m_nMinDispOffset+1);
    cv::Mat oOutput(m_oGridSize,CV_8UC3);
    for(int nRowIdx=0; nRowIdx<m_aaStereoLabelings[nLayerIdx][nCamIdx].rows; ++nRowIdx) {
        for(int nColIdx=0; nColIdx<m_aaStereoLabelings[nLayerIdx][nCamIdx].cols; ++nColIdx) {
            const size_t nLUTNodeIdx = nRowIdx*m_oGridSize[1]+nColIdx;
            const StereoNodeInfo& oNode = m_vStereoNodeMap[nLUTNodeIdx];
            const OutputLabelType nRealLabel = getRealLabel(m_aaStereoLabelings[nLayerIdx][nCamIdx](nRowIdx,nColIdx));
            if(nRealLabel==s_nDontCareLabel)
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(0,128,128);
            else if(nRealLabel==s_nOccludedLabel)
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(128,0,128);
            else {
                const uchar nIntensity = uchar((nRealLabel-m_nMinDispOffset)*fRescaleFact);
                if(oNode.bNearBorders)
                    oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(0,0,nIntensity);
                //else if( has invalid offset desc at max disp )
                //    oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b(uchar(nIntensity/2),uchar(nIntensity/2),nIntensity);
                else
                    oOutput.at<cv::Vec3b>(nRowIdx,nColIdx) = cv::Vec3b::all(nIntensity);
            }
        }
    }
    return oOutput;
}

cv::Mat SegmMatcher::GraphModelData::getAssocCountsMapDisplay() const {
    lvDbgExceptionWatch;
    lvAssert(m_nMaxDispOffset>m_nMinDispOffset);
    lvAssert(!m_oAssocCounts.empty() && m_oAssocCounts.rows==int(m_oGridSize(0)));
    lvAssert(m_oAssocCounts.cols==int((m_oGridSize(1)+m_nMaxDispOffset)/m_nDispOffsetStep));
    double dMax;
    cv::minMaxIdx(m_oAssocCounts,nullptr,&dMax);
    const float fRescaleFact = float(UCHAR_MAX)/(int(dMax)+1);
    cv::Mat oOutput(int(m_oGridSize(0)),int(m_oGridSize(1)+m_nMaxDispOffset),CV_8UC3);
    const int nColIdxStart = ((m_nPrimaryCamIdx==size_t(1))?0:-int(m_nMaxDispOffset));
    const int nColIdxEnd = ((m_nPrimaryCamIdx==size_t(1))?int(m_oGridSize(1)+m_nMaxDispOffset):int(m_oGridSize(1)));
    for(int nRowIdx=0; nRowIdx<int(m_oGridSize(0)); ++nRowIdx) {
        for(int nColIdx=nColIdxStart; nColIdx<nColIdxEnd; ++nColIdx) {
            const AssocCountType nCount = getAssocCount(nRowIdx,nColIdx);
            if(nColIdx<0 || nColIdx>=int(m_oGridSize(1)))
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx-nColIdxStart) = cv::Vec3b(0,0,uchar(nCount*fRescaleFact));
            else
                oOutput.at<cv::Vec3b>(nRowIdx,nColIdx-nColIdxStart) = cv::Vec3b::all(uchar(nCount*fRescaleFact));
        }
    }
    return oOutput;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

SegmMatcher::StereoGraphInference::StereoGraphInference(GraphModelData& oData) :
        m_oData(oData),m_nPrimaryCamIdx(oData.m_nPrimaryCamIdx) {
    lvDbgExceptionWatch;
    lvAssert_(m_nPrimaryCamIdx<getCameraCount(),"camera index out of range");
    lvAssert_(m_oData.m_pStereoModel,"invalid graph");
    const StereoModelType& oGM = *m_oData.m_pStereoModel;
    lvAssert_(oGM.numberOfFactors()>0,"invalid graph");
    for(size_t nFactIdx=0; nFactIdx<oGM.numberOfFactors(); ++nFactIdx)
        lvDbgAssert__(oGM[nFactIdx].numberOfVariables()<=s_nMaxOrder,"graph had some factors of order %d (max allowed is %d)",(int)oGM[nFactIdx].numberOfVariables(),(int)s_nMaxOrder);
    lvDbgAssert_(oGM.numberOfVariables()>0 && oGM.numberOfVariables()<=(IndexType)m_oData.m_oGridSize.total(),"graph node count must match grid size");
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<oGM.numberOfVariables(); ++nGraphNodeIdx)
        lvDbgAssert_(oGM.numberOfLabels(nGraphNodeIdx)==m_oData.m_vStereoLabels.size(),"graph nodes must all have the same number of labels");
}

std::string SegmMatcher::StereoGraphInference::name() const {
    return std::string("litiv-stereo-matcher");
}

const StereoModelType& SegmMatcher::StereoGraphInference::graphicalModel() const {
    lvDbgExceptionWatch;
    lvDbgAssert(m_oData.m_pStereoModel);
    return *m_oData.m_pStereoModel;
}

opengm::InferenceTermination SegmMatcher::StereoGraphInference::infer() {
    lvDbgExceptionWatch;
    return m_oData.infer();
}

void SegmMatcher::StereoGraphInference::setStartingPoint(typename std::vector<InternalLabelType>::const_iterator begin) {
    lvDbgExceptionWatch;
    lvDbgAssert_(lv::filter_out(lv::unique(begin,begin+m_oData.m_oGridSize.total()),lv::make_range(InternalLabelType(0),InternalLabelType(m_oData.m_vStereoLabels.size()-1))).empty(),"provided labeling possesses invalid/out-of-range labels");
    lvDbgAssert_(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].isContinuous() && m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].total()==m_oData.m_oGridSize.total(),"unexpected internal labeling size");
    std::copy_n(begin,m_oData.m_oGridSize.total(),m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].begin());
}

void SegmMatcher::StereoGraphInference::setStartingPoint(const cv::Mat_<OutputLabelType>& oLabeling) {
    lvDbgExceptionWatch;
    lvDbgAssert_(lv::filter_out(lv::unique(oLabeling.begin(),oLabeling.end()),m_oData.m_vStereoLabels).empty(),"provided labeling possesses invalid/out-of-range labels");
    lvAssert_(m_oData.m_oGridSize==oLabeling.size && oLabeling.isContinuous(),"provided labeling must fit grid size & be continuous");
    std::transform(oLabeling.begin(),oLabeling.end(),m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].begin(),[&](const OutputLabelType& nRealLabel){return m_oData.getInternalLabel(nRealLabel);});
}

opengm::InferenceTermination SegmMatcher::StereoGraphInference::arg(std::vector<InternalLabelType>& oLabeling, const size_t n) const {
    lvDbgExceptionWatch;
    if(n==1) {
        lvDbgAssert_(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].total()==m_oData.m_oGridSize.total(),"mismatch between internal graph label count and labeling mat size");
        oLabeling.resize(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].total());
        std::copy(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].begin(),m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].end(),oLabeling.begin()); // the labels returned here are NOT the 'real' ones!
        return opengm::InferenceTermination::NORMAL;
    }
    return opengm::InferenceTermination::UNKNOWN;
}

void SegmMatcher::StereoGraphInference::getOutput(cv::Mat_<OutputLabelType>& oLabeling) const {
    lvDbgExceptionWatch;
    lvDbgAssert_(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].isContinuous() && m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].total()==m_oData.m_oGridSize.total(),"unexpected internal labeling size");
    oLabeling.create(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].size());
    lvDbgAssert_(oLabeling.isContinuous(),"provided matrix must be continuous for in-place label transform");
    std::transform(m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].begin(),m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].end(),oLabeling.begin(),[&](const InternalLabelType& nLabel){return m_oData.getRealLabel(nLabel);});
}

SegmMatcher::ValueType SegmMatcher::StereoGraphInference::value() const {
    lvDbgExceptionWatch;
    lvDbgAssert_(m_oData.m_oGridSize.dims()==2 && m_oData.m_oGridSize==m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].size,"output labeling must be a 2d grid");
    const ValueType tTotAssocCost = m_oData.calcTotalAssocCost();
    struct GraphNodeLabelIter {
        GraphNodeLabelIter(size_t nCamIdx, const GraphModelData& oData) : m_oData(oData),m_nPrimaryCamIdx(nCamIdx) {}
        InternalLabelType operator[](size_t nGraphNodeIdx) {
            lvDbgAssert(nGraphNodeIdx<m_oData.m_nValidStereoGraphNodes);
            const size_t nLUTNodeIdx = m_oData.m_vStereoGraphIdxToMapIdxLUT[nGraphNodeIdx];
            lvDbgAssert(nLUTNodeIdx<m_oData.m_oGridSize.total() && nLUTNodeIdx<m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].total());
            return ((InternalLabelType*)m_oData.m_aaStereoLabelings[0][m_nPrimaryCamIdx].data)[nLUTNodeIdx];
        }
        const GraphModelData& m_oData;
        const size_t m_nPrimaryCamIdx;
    } oLabelIter(m_nPrimaryCamIdx,m_oData);
    const ValueType tTotStereoLabelCost = m_oData.m_pStereoModel->evaluate(oLabelIter);
    return tTotAssocCost+tTotStereoLabelCost;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

SegmMatcher::ResegmGraphInference::ResegmGraphInference(GraphModelData& oData) :
        m_oData(oData) {
    lvDbgExceptionWatch;
    lvAssert_(m_oData.m_pResegmModel,"invalid graph");
    const ResegmModelType& oGM = *m_oData.m_pResegmModel;
    lvAssert_(oGM.numberOfFactors()>0,"invalid graph");
    for(size_t nFactIdx=0; nFactIdx<oGM.numberOfFactors(); ++nFactIdx)
        lvDbgAssert__(oGM[nFactIdx].numberOfVariables()<=s_nMaxOrder,"graph had some factors of order %d (max allowed is %d)",(int)oGM[nFactIdx].numberOfVariables(),(int)s_nMaxOrder);
    lvDbgAssert_(oGM.numberOfVariables()>0 && oGM.numberOfVariables()<=(IndexType)m_oData.m_oGridSize.total()*getTemporalLayerCount()*getCameraCount(),"graph node count must match grid size times layers times cam count");
    for(size_t nGraphNodeIdx=0; nGraphNodeIdx<oGM.numberOfVariables(); ++nGraphNodeIdx)
        lvDbgAssert_(oGM.numberOfLabels(nGraphNodeIdx)==size_t(2),"graph nodes must all have the same number of labels");
}

std::string SegmMatcher::ResegmGraphInference::name() const {
    return std::string("litiv-segm-matcher");
}

const ResegmModelType& SegmMatcher::ResegmGraphInference::graphicalModel() const {
    lvDbgExceptionWatch;
    lvDbgAssert(m_oData.m_pResegmModel);
    return *m_oData.m_pResegmModel;
}

opengm::InferenceTermination SegmMatcher::ResegmGraphInference::infer() {
    lvDbgExceptionWatch;
    return m_oData.infer();
}

void SegmMatcher::ResegmGraphInference::setStartingPoint(typename std::vector<InternalLabelType>::const_iterator begin) {
    lvDbgExceptionWatch;
    lvDbgAssert_(lv::filter_out(lv::unique(begin,begin+m_oData.m_oGridSize.total()),std::vector<InternalLabelType>{s_nBackgroundLabelIdx,s_nForegroundLabelIdx}).empty(),"provided labeling possesses invalid/out-of-range labels");
    lvDbgAssert_(m_oData.m_oSuperStackedResegmLabeling.isContinuous() && m_oData.m_oSuperStackedResegmLabeling.total()==m_oData.m_oGridSize.total()*getTemporalLayerCount()*getCameraCount(),"unexpected internal labeling size");
    std::copy_n(begin,m_oData.m_oGridSize.total()*getTemporalLayerCount()*getCameraCount(),m_oData.m_oSuperStackedResegmLabeling.begin());
}

void SegmMatcher::ResegmGraphInference::setStartingPoint(const TemporalArray<CamArray<cv::Mat_<OutputLabelType>>>& aaLabeling) {
    lvDbgExceptionWatch;
    for(size_t nLayerIdx=0; nLayerIdx<getTemporalLayerCount(); ++nLayerIdx) {
        for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
            lvDbgAssert_(lv::filter_out(lv::unique(aaLabeling[nLayerIdx][nCamIdx].begin(),aaLabeling[nLayerIdx][nCamIdx].end()),std::vector<OutputLabelType>{s_nBackgroundLabel,s_nForegroundLabel}).empty(),"provided labeling possesses invalid/out-of-range labels");
            lvAssert_(m_oData.m_oGridSize==aaLabeling[nLayerIdx][nCamIdx].size && aaLabeling[nLayerIdx][nCamIdx].isContinuous(),"provided labeling must fit grid size & be continuous");
            std::transform(aaLabeling[nLayerIdx][nCamIdx].begin(),aaLabeling[nLayerIdx][nCamIdx].end(),m_oData.m_aaResegmLabelings[nLayerIdx][nCamIdx].begin(),[&](const OutputLabelType& nRealLabel){return nRealLabel?s_nForegroundLabelIdx:s_nBackgroundLabelIdx;});
        }
    }
}

opengm::InferenceTermination SegmMatcher::ResegmGraphInference::arg(std::vector<InternalLabelType>& oLabeling, const size_t n) const {
    lvDbgExceptionWatch;
    if(n==1) {
        lvDbgAssert_(m_oData.m_oSuperStackedResegmLabeling.total()==m_oData.m_oGridSize.total()*getTemporalLayerCount()*getCameraCount(),"mismatch between internal graph label count and labeling mat size");
        oLabeling.resize(m_oData.m_oSuperStackedResegmLabeling.total());
        std::copy(m_oData.m_oSuperStackedResegmLabeling.begin(),m_oData.m_oSuperStackedResegmLabeling.end(),oLabeling.begin()); // the labels returned here are NOT the 'real' ones (same values, different type)
        return opengm::InferenceTermination::NORMAL;
    }
    return opengm::InferenceTermination::UNKNOWN;
}

void SegmMatcher::ResegmGraphInference::getOutput(TemporalArray<CamArray<cv::Mat_<OutputLabelType>>>& aaLabeling) const {
    lvDbgExceptionWatch;
    for(size_t nLayerIdx=0; nLayerIdx<getTemporalLayerCount(); ++nLayerIdx) {
        for(size_t nCamIdx=0; nCamIdx<getCameraCount(); ++nCamIdx) {
            lvDbgAssert_(m_oData.m_aaResegmLabelings[nLayerIdx][nCamIdx].isContinuous() && m_oData.m_aaResegmLabelings[nLayerIdx][nCamIdx].total()==m_oData.m_oGridSize.total(),"unexpected internal labeling size");
            aaLabeling[nLayerIdx][nCamIdx].create(m_oData.m_aaResegmLabelings[nLayerIdx][nCamIdx].size());
            lvDbgAssert_(aaLabeling[nLayerIdx][nCamIdx].isContinuous(),"provided matrix must be continuous for in-place label transform");
            std::transform(m_oData.m_aaResegmLabelings[nLayerIdx][nCamIdx].begin(),m_oData.m_aaResegmLabelings[nLayerIdx][nCamIdx].end(),aaLabeling[nLayerIdx][nCamIdx].begin(),[&](const InternalLabelType& nLabel){return nLabel?s_nForegroundLabel:s_nBackgroundLabel;});
        }
    }
}

SegmMatcher::ValueType SegmMatcher::ResegmGraphInference::value() const {
    lvDbgExceptionWatch;
    struct GraphNodeLabelIter {
        GraphNodeLabelIter(const GraphModelData& oData) : m_oData(oData) {}
        InternalLabelType operator[](size_t nGraphNodeIdx) {
            lvDbgAssert(nGraphNodeIdx<m_oData.m_nValidResegmGraphNodes);
            const size_t nLUTNodeIdx = m_oData.m_vResegmGraphIdxToMapIdxLUT[nGraphNodeIdx];
            lvDbgAssert(nLUTNodeIdx<m_oData.m_oSuperStackedResegmLabeling.total());
            return ((InternalLabelType*)m_oData.m_oSuperStackedResegmLabeling.data)[nLUTNodeIdx];
        }
        const GraphModelData& m_oData;
    } oLabelIter(m_oData);
    const ValueType tTotResegmLabelCost = m_oData.m_pResegmModel->evaluate(oLabelIter);
    return tTotResegmLabelCost;
}
