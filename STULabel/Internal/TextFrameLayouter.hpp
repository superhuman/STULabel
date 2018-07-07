// Copyright 2017–2018 Stephan Tolksdorf

#import "ShapedString.hpp"
#import "TextFrame.hpp"
#import "TextStyleBuffer.hpp"

#import "stu/UniquePtr.hpp"

namespace stu_label {

using CTTypesetter = RemovePointer<CTTypesetterRef>;

class TextFrameLayouter {
public:
  TextFrameLayouter(const ShapedString&, Range<Int32> stringRange,
                    STUDefaultTextAlignment defaultTextAlignment,
                    const STUCancellationFlag* cancellationFlag);

  ~TextFrameLayouter();

  STU_INLINE_T
  bool isCancelled() const { return STUCancellationFlagGetValue(&cancellationFlag_); }

  struct ScaleInfo {
    CGFloat scale;
    Float64 inverseScale;
    Float64 firstParagraphFirstLineOffset;
    STUFirstLineOffsetType firstParagraphFirstLineOffsetType;
    STUBaselineAdjustment baselineAdjustment;
  };

  // Only layoutAndScale and layout check for cancellation while running.
  // After cancellation the TextFrameLayouter can be safely destructed,
  // but no other method may be called.

  // The passed in options pointer must stay valid until after any subsequent call to
  // estimateScaleFactorNeededToFit.

  void layoutAndScale(Size<Float64> frameSize, const STUTextFrameOptions* __nonnull options);

  void layout(Size<Float64> inverselyScaledFrameSize, ScaleInfo scaleInfo,
              Int maxLineCount, const STUTextFrameOptions* __nonnull options);

  template <STUTextLayoutMode mode>
  static MinLineHeightInfo minLineHeightInfo(const LineHeightParams& params,
                                             const MinFontMetrics& minFontMetrics);

  STUTextLayoutMode layoutMode() const { return layoutMode_; }

  struct ScaleFactorEstimate {
    Float64 value;
    bool isAccurate;
  };

  Float64 calculateMaxScaleFactorForCurrentLineBreaks() const;

  /// Usually returns an exact value or a lower bound that is quite close to the exact value.
  /// Paragraphs with varying line heights affect the accuracy negatively.
  /// Hyphenation opportunities are currently ignored, so the estimate can be farther off if the
  /// text involves multiline paragraphs with hyphenation factors greater 0.
  ///
  /// @param accuracy The desired absolute accuracy of the returned estimate.
  ScaleFactorEstimate estimateScaleFactorNeededToFit(Float64 frameHeight, Int32 maxLineCount,
                                                     Float64 minScale, Float64 accuracy) const;

  bool needToJustifyLines() const { return needToJustifyLines_; }

  void justifyLinesWhereNecessary();

  const ScaleInfo& scaleInfo() const { return scaleInfo_; }

  Size<Float64> inverselyScaledFrameSize() const { return inverselyScaledFrameSize_; }

  /// Is reset to 0 at the beginning of layoutAndScale.
  UInt32 layoutCallCount() const { return layoutCallCount_; }

  const NSAttributedStringRef& attributedString() const {
    return attributedString_;
  }

  Range<Int32> rangeInOriginalString() const {
    return {stringRange_.start, clippedStringRangeEnd_};
  }

  bool rangeInOriginalStringIsFullString() const {
    return stringRangeIsFullString_ && !textIsClipped();
  }

  bool textIsClipped() const {
    return stringRange_.end != clippedStringRangeEnd_;
  }

  STU_INLINE
  ArrayRef<const ShapedString::Paragraph> originalStringParagraphs() const {
    return {stringParas_, paras_.count(), unchecked};
  }

  ArrayRef<const TextFrameParagraph> paragraphs() const {
    return {paras_.begin(), clippedParagraphCount_, unchecked};
  }

  ArrayRef<const TextFrameLine> lines() const { return lines_; }

  Int32 truncatedStringLength() const {
    return paras_.isEmpty() ? 0 : paras_[$ - 1].rangeInTruncatedString.end;
  }

  ArrayRef<const FontRef> fonts() const { return tokenStyleBuffer_.fonts(); }

  ArrayRef<const ColorRef> colors() const { return tokenStyleBuffer_.colors(); }

  TextStyleSpan originalStringStyles() const {
    return {originalStringStyles_.firstStyle, clippedOriginalStringTerminatorStyle_};
  }

  ArrayRef<const Byte> truncationTokenTextStyleData() const { return tokenStyleBuffer_.data(); }

  void relinquishOwnershipOfCTLinesAndParagraphTruncationTokens() {
    ownsCTLinesAndParagraphTruncationTokens_ = false;
  }

  static Float32 intraParagraphBaselineDistanceForLinesLike(const TextFrameLine& line,
                                                            const ShapedString::Paragraph& para);

  LocalFontInfoCache& localFontInfoCache() { return localFontInfoCache_; }

private:
  struct Indentations {
    Float64 left;
    Float64 right;
    Float64 head;

    Indentations(const ShapedString::Paragraph& para,
                 bool isFirstLineInPara,
                 Float64 inverselyScaledFrameWidth,
                 const TextFrameLayouter::ScaleInfo& scaleInfo)
    {
      // We don't scale the horizontal indentation.
      Float64 leftIndent  = para.paddingLeft*scaleInfo.inverseScale;
      Float64 rightIndent = para.paddingRight*scaleInfo.inverseScale;
      if (leftIndent < 0) {
        leftIndent += inverselyScaledFrameWidth;
      }
      if (rightIndent < 0) {
        rightIndent += inverselyScaledFrameWidth;
      }
      if (isFirstLineInPara) {
        leftIndent  += para.firstLineLeftIndent;
        rightIndent += para.firstLineRightIndent;
      }
      this->left = leftIndent;
      this->right = rightIndent;
      this->head = para.baseWritingDirection == STUWritingDirectionLeftToRight
                 ? leftIndent : rightIndent;
    }
  };


  struct MaxWidthAndHeadIndent {
    Float64 maxWidth;
    Float64 headIndent;
  };
  struct Hyphen : Parameter<Hyphen, Char32> { using Parameter::Parameter; };
  struct TrailingWhitespaceStringLength : Parameter<TrailingWhitespaceStringLength, Int> {
    using Parameter::Parameter;
  };

  void breakLine(TextFrameLine& line, Int paraStringEndIndex);

  struct BreakLineAtStatus {
    bool success;
    Float64 ctLineWidthWithoutHyphen;
  };

  BreakLineAtStatus breakLineAt(TextFrameLine& line, Int stringIndex, Hyphen hyphen,
                                TrailingWhitespaceStringLength) const;

  bool hyphenateLineInRange(TextFrameLine& line, Range<Int> stringRange);

  void truncateLine(TextFrameLine& line, Int32 stringEndIndex, Range<Int32> truncatableRange,
                    CTLineTruncationType, NSAttributedString* __nullable token,
                    __nullable STUTruncationRangeAdjuster,
                    STUTextFrameParagraph& para, TextStyleBuffer& tokenStyleBuffer) const;

  void justifyLine(STUTextFrameLine& line) const;

  const TextStyle* initializeTypographicMetricsOfLine(TextFrameLine& line);

  const TextStyle* firstOriginalStringStyle(const STUTextFrameLine& line) const {
    return reinterpret_cast<const TextStyle*>(originalStringStyles_.dataBegin()
                                              + line._textStylesOffset);
  }
  const TextStyle* __nullable firstTruncationTokenStyle(const STUTextFrameLine& line) const {
    STU_DEBUG_ASSERT(line._initStep != 1);
    if (!line.hasTruncationToken) return nullptr;
    return reinterpret_cast<const TextStyle*>(tokenStyleBuffer_.data().begin()
                                              + line._tokenStylesOffset);
  }


  Float64 scaleFactorNeededToFitWidth() const;

  static void addAttributesNotYetPresentInAttributedString(
                NSMutableAttributedString*, NSRange, NSDictionary<NSAttributedStringKey, id>*);

  /// @pre !line.hasTruncationToken
  Float64 trailingWhitespaceWidth(const TextFrameLine& line) const;

  Float64 estimateTailTruncationTokenWidth(const TextFrameLine& line) const;

  class SavedLayout {
    friend TextFrameLayouter;
    
    struct Data {
      UInt size;
      ArrayRef<TextFrameParagraph> paragraphs;
      ArrayRef<TextFrameLine> lines;
      ArrayRef<Byte> tokenStyleData;
      ScaleInfo scaleInfo;
      Size<Float64> inverselyScaledFrameSize;
      bool needToJustifyLines;
      bool mayExceedMaxWidth;
      Int32 clippedStringRangeEnd;
      Int clippedParagraphCount;
      const TextStyle* clippedOriginalStringTerminatorStyle;
    };

    Data* data_{};

  public:
    SavedLayout() = default;

    ~SavedLayout() { if (data_) clear(); }
    
    SavedLayout(const SavedLayout&) = delete;
    SavedLayout& operator=(const SavedLayout&) = delete;

    void clear();
  };

  void destroyLinesAndParagraphs();

  void saveLayoutTo(SavedLayout&);
  void restoreLayoutFrom(SavedLayout&&);

  struct InitData {
    const STUCancellationFlag& cancellationFlag;
    CTTypesetter* const typesetter;
    NSAttributedStringRef attributedString;
    Range<Int> stringRange;
    ArrayRef<const TruncationScope> truncationScopes;
    ArrayRef<const ShapedString::Paragraph> stringParas;
    TempArray<TextFrameParagraph> paras;
    TextStyleSpan stringStyles;
    ArrayRef<const FontMetrics> stringFontMetrics;
    ArrayRef<const ColorRef> stringColorInfos;
    ArrayRef<const TextStyleBuffer::ColorHashBucket> stringColorHashBuckets;
    bool stringRangeIsFullString;

    static InitData create(const ShapedString&, Range<Int32> stringRange,
                           STUDefaultTextAlignment defaultTextAlignment,
                           Optional<const STUCancellationFlag&> cancellationFlag);
  };
  explicit TextFrameLayouter(InitData init);

  const STUCancellationFlag& cancellationFlag_;
  CTTypesetter* const typesetter_;
  const NSAttributedStringRef attributedString_;
  const TextStyleSpan originalStringStyles_;
  const ArrayRef<const FontMetrics> originalStringFontMetrics_;
  const ArrayRef<const TruncationScope> truncationScopes_;
  const ShapedString::Paragraph* stringParas_;
  const Range<Int32> stringRange_;
  TempArray<TextFrameParagraph> paras_;
  TempVector<TextFrameLine> lines_{Capacity{16}};
  ScaleInfo scaleInfo_{.scale = 1, .inverseScale = 1};
  Size<Float64> inverselyScaledFrameSize_{};
  const bool stringRangeIsFullString_;
  STUTextLayoutMode layoutMode_{};
  bool needToJustifyLines_{};
  bool mayExceedMaxWidth_{};
  bool ownsCTLinesAndParagraphTruncationTokens_{true};
  UInt32 layoutCallCount_{};
  Int32 clippedStringRangeEnd_{};
  Int clippedParagraphCount_{};
  const TextStyle* clippedOriginalStringTerminatorStyle_;
  /// A cached CFLocale instance for hyphenation purposes.
  RC<CFLocale> cachedLocale_;
  CFString* cachedLocaleId_{};
  Float64 lineMaxWidth_;
  Float64 lineHeadIndent_;
  Float64 hyphenationFactor_;
  const STUTextFrameOptions* __unsafe_unretained options_;
  STULastHyphenationLocationInRangeFinder __nullable __unsafe_unretained
    lastHyphenationLocationInRangeFinder_;
  LocalFontInfoCache localFontInfoCache_;
  TextStyleBuffer tokenStyleBuffer_;
  TempVector<FontMetrics> tokenFontMetrics_;
};

} // namespace stu_label
