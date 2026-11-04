// Developer bridge: exactly the production local-expression sampler/landmarks.
#include "../src/cat_species.hpp"
extern "C" int expression_preview(const std::uint32_t* source,int width,int height,
                                   std::uint32_t* output,int character,int expression,
                                   const std::uint32_t* closed,int closedWidth,int closedHeight) {
    if(character<0||character>=cat::kCharacterCount)return 0;
    cat::expression::Face face;
    if(expression==1)face.blink=1;
    if(expression==2){face.blink=1;face.mouth=1;}
    if(expression==3){face.blink=.35;face.smile=.7;}
    if(expression==4){face.wide=1;face.brow=-.75;face.mouth=.35;}
    return cat::expression::Apply(source,width,height,output,face,
        cat::species::FaceFor(static_cast<cat::Character>(character)),{},
        {closed,closedWidth,closedHeight,cat::species::ClosedFaceFor(static_cast<cat::Character>(character))});
}
