//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/pxr.h"

#include "pxr/imaging/garch/glDebugWindow.h"

#include "pxr/imaging/hdSt/unitTestGLDrawing.h"
#include "pxr/imaging/hdSt/unitTestHelper.h"

#include "pxr/imaging/hdx/selectionTask.h"
#include "pxr/imaging/hdx/tokens.h"
#include "pxr/imaging/hdx/renderTask.h"
#include "pxr/imaging/hdx/unitTestDelegate.h"
#include "pxr/imaging/hdx/unitTestUtils.h"

#include "pxr/base/tf/errorMark.h"

#include <iostream>
#include <unordered_set>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    (pickables)
);

class Hdx_TestDriver : public HdSt_TestDriverBase<Hdx_UnitTestDelegate>
{
public:
    Hdx_TestDriver();

    void DrawWithSelection(GfVec4d const &viewport, 
        HdxSelectionTrackerSharedPtr selTracker);

    HdSelectionSharedPtr Pick(GfVec2i const &startPos, GfVec2i const &endPos,
        int width, int height, GfFrustum const &frustum, 
        GfMatrix4d const &viewMatrix);

protected:
    void _Init(HdReprSelector const &reprSelector) override;

private:
    HdRprimCollection _pickablesCol;
};

Hdx_TestDriver::Hdx_TestDriver()
{
    _Init(HdReprSelector(HdReprTokens->hull));
}

void
Hdx_TestDriver::_Init(HdReprSelector const &reprSelector)
{   
    _SetupSceneDelegate();
    
    Hdx_UnitTestDelegate &delegate = GetDelegate();

    // prepare render task
    SdfPath renderSetupTask("/renderSetupTask");
    SdfPath renderTask("/renderTask");
    SdfPath selectionTask("/selectionTask");
    SdfPath pickTask("/pickTask");
    delegate.AddRenderSetupTask(renderSetupTask);
    delegate.AddRenderTask(renderTask);
    delegate.AddSelectionTask(selectionTask);
    delegate.AddPickTask(pickTask);

    // render task parameters.
    VtValue vParam = delegate.GetTaskParam(renderSetupTask, HdTokens->params);
    HdxRenderTaskParams param = vParam.Get<HdxRenderTaskParams>();
    param.enableLighting = true; // use default lighting
    delegate.SetTaskParam(renderSetupTask, HdTokens->params, VtValue(param));
    delegate.SetTaskParam(renderTask, HdTokens->collection,
        VtValue(HdRprimCollection(HdTokens->geometry, reprSelector)));

    HdxSelectionTaskParams selParam;
    selParam.enableSelectionHighlight = true;
    selParam.selectionColor = GfVec4f(1, 1, 0, 1);
    selParam.locateColor = GfVec4f(1, 0, 1, 1);
    delegate.SetTaskParam(selectionTask, HdTokens->params, VtValue(selParam));
    
    // picking related init
    // The collection used for the ID render defaults to including the root path
    // which essentially means that all scene graph prims are pickable.
    // 
    // Worth noting that the collection's repr is set to refined (and not 
    // hull). When a prim has an authored repr, we'll use that instead, as
    // the collection's forcedRepr defaults to false.
    _pickablesCol = HdRprimCollection(_tokens->pickables,
                        HdReprSelector(HdReprTokens->refined));
    // We have to unfortunately explictly add collections besides 'geometry'
    // See HdRenderIndex constructor.
    delegate.GetRenderIndex().GetChangeTracker().AddCollection(
        _tokens->pickables);
}

void
Hdx_TestDriver::DrawWithSelection(GfVec4d const &viewport, 
    HdxSelectionTrackerSharedPtr selTracker)
{
    SdfPath renderSetupTask("/renderSetupTask");
    SdfPath renderTask("/renderTask");
    SdfPath selectionTask("/selectionTask");

    HdxRenderTaskParams param = GetDelegate().GetTaskParam(
        renderSetupTask, HdTokens->params).Get<HdxRenderTaskParams>();
    param.viewport = viewport;
    param.aovBindings = _aovBindings;
    GetDelegate().SetTaskParam(
        renderSetupTask, HdTokens->params, VtValue(param));

    HdTaskSharedPtrVector tasks;
    tasks.push_back(GetDelegate().GetRenderIndex().GetTask(renderSetupTask));
    tasks.push_back(GetDelegate().GetRenderIndex().GetTask(renderTask));
    tasks.push_back(GetDelegate().GetRenderIndex().GetTask(selectionTask));

    _GetEngine()->SetTaskContextData(
        HdxTokens->selectionState, VtValue(selTracker));
    _GetEngine()->Execute(&GetDelegate().GetRenderIndex(), &tasks);
}

HdSelectionSharedPtr
Hdx_TestDriver::Pick(GfVec2i const &startPos, GfVec2i const &endPos,
    int width, int height, GfFrustum const &frustum, 
    GfMatrix4d const &viewMatrix)
{
    HdxPickHitVector allHits;
    HdxPickTaskContextParams p;
    p.resolution = HdxUnitTestUtils::CalculatePickResolution(
        startPos, endPos, GfVec2i(4,4));
    p.resolveMode = HdxPickTokens->resolveUnique;
    p.viewMatrix = viewMatrix;
    p.projectionMatrix = HdxUnitTestUtils::ComputePickingProjectionMatrix(
        startPos, endPos, GfVec2i(width, height), frustum);
    p.collection = _pickablesCol;
    p.outHits = &allHits;

    HdTaskSharedPtrVector tasks;
    tasks.push_back(GetDelegate().GetRenderIndex().GetTask(
        SdfPath("/pickTask")));
    VtValue pickParams(p);
    _GetEngine()->SetTaskContextData(HdxPickTokens->pickParams, pickParams);
    _GetEngine()->Execute(&GetDelegate().GetRenderIndex(), &tasks);

    return HdxUnitTestUtils::TranslateHitsToSelection(
        p.pickTarget, HdSelection::HighlightModeSelect, allHits);
}

// --------------------------------------------------------------------------

class My_TestGLDrawing : public HdSt_UnitTestGLDrawing
{
public:
    My_TestGLDrawing()
    {
        SetCameraRotate(0, 0);
        SetCameraTranslate(GfVec3f(0));
    }

    void DrawScene();
    void DrawMarquee();

    // HdSt_UnitTestGLDrawing overrides
    void InitTest() override;
    void UninitTest() override;
    void DrawTest() override;
    void OffscreenTest() override;
    void Present(uint32_t framebuffer) override;
    void MousePress(int button, int x, int y, int modKeys) override;
    void MouseRelease(int button, int x, int y, int modKeys) override;
    void MouseMove(int x, int y, int modKeys) override;

protected:
    void _InitScene();
    HdSelectionSharedPtr _Pick(GfVec2i const& startPos, GfVec2i const& endPos);

private:
    std::unique_ptr<Hdx_TestDriver> _driver;
    
    HdxUnitTestUtils::Marquee _marquee;
    HdxSelectionTrackerSharedPtr _selTracker;

    GfVec2i _startPos, _endPos;
};

////////////////////////////////////////////////////////////

static GfMatrix4d
_GetTranslate(float tx, float ty, float tz)
{
    GfMatrix4d m(1.0f);
    m.SetRow(3, GfVec4f(tx, ty, tz, 1.0));
    return m;
}

void
My_TestGLDrawing::InitTest()
{
    _driver = std::make_unique<Hdx_TestDriver>();

    _selTracker.reset(new HdxSelectionTracker);

    // prepare scene
    _InitScene();
    SetCameraTranslate(GfVec3f(0, 0, -20));

    _marquee.InitGLResources();

    _driver->SetClearColor(GfVec4f(0.1f, 0.1f, 0.1f, 1.0f));
    _driver->SetClearDepth(1.0f);
    _driver->SetupAovs(GetWidth(), GetHeight());
}

void
My_TestGLDrawing::UninitTest()
{
    _marquee.DestroyGLResources();
}

void
My_TestGLDrawing::_InitScene()
{
    Hdx_UnitTestDelegate &delegate = _driver->GetDelegate();

    delegate.AddCube(SdfPath("/cube1"), _GetTranslate(-5, 0, 5));
    delegate.AddCube(SdfPath("/cube2"), _GetTranslate(-5, 0,-5));
}

HdSelectionSharedPtr
My_TestGLDrawing::_Pick(GfVec2i const& startPos, GfVec2i const& endPos)
{
    return _driver->Pick(startPos, endPos, GetWidth(), GetHeight(),
        GetFrustum(), GetViewMatrix());
}

void
My_TestGLDrawing::DrawTest()
{
    DrawScene();
    DrawMarquee();
}

void
My_TestGLDrawing::OffscreenTest()
{
    Hdx_UnitTestDelegate &delegate = _driver->GetDelegate();

    DrawScene();
    _driver->WriteToFile("color", "color1_unselected.png");

    // This test uses 2 collections:
    // (i)  geometry
    // (ii) pickables
    // Picking in this test uses the 'refined' repr. See the collection
    // created in Pick(..) for additional notes.
    // 
    // We want to ensure that these collections' command buffers are updated
    // correctly in the following scenarios:
    // - changing a prim's refine level when using a different non-authored
    // repr from that in the pickables collection 
    // - changing a prim's repr accounts for refineLevel dirtyness intercepted
    // by the picking task.
    // 
    // This test is run with the scene repr = 'hull'. We want to test several
    // cases:
    // (a) Change refine level on prim A with repr hull ==> Drawn image should
    //  not change, since hull doesn't update topology on refinement. The
    //  picking collection will however reflect this change (making this a
    //  weird scenario)
    // 
    // (b) Change repr on prim B ==> Drawn image should reflect the new repr
    //          
    // (c) Change repr on prim A ==> Drawn image should reflect the refineLevel
    //  update in (a) if its repr supports it (refined, refinedWire, refinedWireOnSurf)
    //  
    // (d) Change refine level on prim B ==> Drawn image should reflect the refineLevel
    //  if its repr supports it (refined, refinedWire, refinedWireOnSurf)

    HdSelection::HighlightMode mode = HdSelection::HighlightModeSelect;
    HdSelectionSharedPtr selection;

    // (a)
    {
        std::cout << "Changing refine level of cube1" << std::endl;
        delegate.SetRefineLevel(SdfPath("/cube1"), 2);
        // The repr corresponding to picking (refined) would be the one that
        // handles the DirtyDisplayStyle bit, since we don't call DrawScene()
        // before Pick(). We don't explicitly mark the collections dirty in this
        // case, since refine level changes trigger change tracker garbage 
        // collection and the render delegate marks all collections dirty.
        // See HdStRenderDelegate::CommitResources
        // XXX: This is hacky.
        // 
        // Since we're not overriding the scene repr, cube1 will still
        // appear unrefined, since it defaults to the hull repr.
        // However, the picking collection will render the refined version, and
        // we won't be able to select cube1 by picking the unrefined version's
        // left top corner.
        selection = _Pick(GfVec2i(138, 60), GfVec2i(138, 60));
        _selTracker->SetSelection(selection);
        DrawScene();
        _driver->WriteToFile("color", "color2_refine_wont_change_cube1.png");
        TF_VERIFY(selection->GetSelectedPrimPaths(mode).size() == 0);
    }

    // (b)
    {
        std::cout << "Changing repr for cube2" << std::endl;
        delegate.SetReprName(SdfPath("/cube2"), 
            HdReprTokens->refinedWireOnSurf);

        selection = _Pick(GfVec2i(152, 376), GfVec2i(152, 376));
        _selTracker->SetSelection(selection);
        DrawScene();
        _driver->WriteToFile("color", "color3_repr_change_cube2.png");
        TF_VERIFY(selection->GetSelectedPrimPaths(mode).size() == 1);
        TF_VERIFY(selection->GetSelectedPrimPaths(mode)[0] == SdfPath("/cube2"));
    }

    // (c)
    {
       std::cout << "Changing repr on cube1" << std::endl;

        delegate.SetReprName(SdfPath("/cube1"), HdReprTokens->refinedWire);

        selection = _Pick(GfVec2i(176, 96), GfVec2i(179, 99));
        _selTracker->SetSelection(selection);
        DrawScene();
        _driver->WriteToFile("color", "color4_repr_and_refine_change_cube1.png");
        TF_VERIFY(selection->GetSelectedPrimPaths(mode).size() == 1);
        TF_VERIFY(selection->GetSelectedPrimPaths(mode)[0] == SdfPath("/cube1"));
    }


    // (d)
    {
        std::cout << "## Changing refine level of cube2 ##" << std::endl;
        delegate.SetRefineLevel(SdfPath("/cube2"), 3);

        selection = _Pick(GfVec2i(152, 376), GfVec2i(152, 376));
        _selTracker->SetSelection(selection);
        DrawScene();
        _driver->WriteToFile("color", "color5_refine_change_cube2.png");
        TF_VERIFY(selection->GetSelectedPrimPaths(mode)[0] == SdfPath("/cube2"));
    }

     // deselect    
    selection = _Pick(GfVec2i(0,0), GfVec2i(0,0));
    _selTracker->SetSelection(selection);
    DrawScene();
    _driver->WriteToFile("color", "color6_unselected.png");
}

void
My_TestGLDrawing::DrawScene()
{
    int width = GetWidth(), height = GetHeight();

    GfMatrix4d viewMatrix = GetViewMatrix();
    GfFrustum frustum = GetFrustum();

    GfVec4d viewport(0, 0, width, height);

    GfMatrix4d projMatrix = frustum.ComputeProjectionMatrix();
    _driver->GetDelegate().SetCamera(viewMatrix, projMatrix);

    _driver->UpdateAovDimensions(width, height);

    _driver->DrawWithSelection(viewport, _selTracker);
}

void
My_TestGLDrawing::DrawMarquee()
{
    _marquee.Draw(GetWidth(), GetHeight(), _startPos, _endPos);
}

void
My_TestGLDrawing::Present(uint32_t framebuffer)
{
    _driver->Present(GetWidth(), GetHeight(), framebuffer);
}

void
My_TestGLDrawing::MousePress(int button, int x, int y, int modKeys)
{
    HdSt_UnitTestGLDrawing::MousePress(button, x, y, modKeys);
    _startPos = _endPos = GetMousePos();
}

void
My_TestGLDrawing::MouseRelease(int button, int x, int y, int modKeys)
{
    HdSt_UnitTestGLDrawing::MouseRelease(button, x, y, modKeys);

    if (!(modKeys & GarchGLDebugWindow::Alt)) {
        HdSelectionSharedPtr selection = _Pick(_startPos, _endPos);
        _selTracker->SetSelection(selection);
    }
    _startPos = _endPos = GfVec2i(0);
}

void
My_TestGLDrawing::MouseMove(int x, int y, int modKeys)
{
    HdSt_UnitTestGLDrawing::MouseMove(x, y, modKeys);

    if (!(modKeys & GarchGLDebugWindow::Alt)) {
        _endPos = GetMousePos();
    }
}

void
BasicTest(int argc, char *argv[])
{
    My_TestGLDrawing driver;

    driver.RunTest(argc, argv);
}

int main(int argc, char *argv[])
{
    TfErrorMark mark;

    BasicTest(argc, argv);

    if (mark.IsClean()) {
        std::cout << "OK" << std::endl;
        return EXIT_SUCCESS;
    } else {
        std::cout << "FAILED" << std::endl;
        return EXIT_FAILURE;
    }
}
