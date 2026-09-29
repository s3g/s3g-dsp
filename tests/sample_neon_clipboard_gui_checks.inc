                if (ok) {
                    failureStage = "Sample Neon pad/layer context menus and undo";
                    auto restore = stackSound; restore.offset = 0;
                    clap_istream_t input {&restore, stateReadWhole};
                    ok = state->load(plugin, &input) && ok;
                    selectPlay(true); clickAt(M::pageCenter(0), M::banks);
                    clickAt(M::padX(0)+M::padWidth*.5, M::padY(0)+M::padHeight*.5);
                    inspectorChoice(0, 0);
                    NSWindow* contextWindow = [[S3GSmokeKeyWindow alloc] initWithContentRect:[parent bounds]
                        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
                    [contextWindow setReleasedWhenClosed:NO]; [contextWindow setContentView:parent];
                    const auto rightClick = [&](double x, double y) {
                        [document mouseDown:[NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                            location:[document convertPoint:NSMakePoint(x*scale, y*scale) toView:nil]
                            modifierFlags:NSEventModifierFlagControl timestamp:0 windowNumber:[contextWindow windowNumber]
                            context:nil eventNumber:0 clickCount:1 pressure:1]];
                        [document mouseUp:mouseEvent(NSEventTypeLeftMouseUp, NSMakePoint(x*scale, y*scale))]; redraw();
                    };
                    const auto chooseContext = [&](double x, double y, unsigned action) {
                        rightClick(x,y);
                        clickAt(std::min(x,double(nativeWidth)-170)+22, y+I::popupRowHeight*(action+.5));
                        plugin->on_main_thread(plugin); redraw();
                    };
                    const auto layerAction = [&](unsigned layer, unsigned action) {
                        chooseContext(I::columnLeft(layer%4,4)+I::columnWidth(4)*.5, I::row(9+layer/4), action);
                    };
                    const auto undo = [&] { clickAt(1198,24); plugin->on_main_thread(plugin); redraw(); };
                    const auto redo = [&] { clickAt(1292,24); plugin->on_main_thread(plugin); redraw(); };
                    const auto before = savedState();
                    layerAction(1,0); // Copy L2 without selecting or auditioning it.
                    ok = savedState().bytes == before.bytes && ok;
                    layerAction(31,1); // Paste into a disabled, empty L32 grid position.
                    const auto pasted = savedState();
                    ok = pasted.bytes.size() > before.bytes.size() && ok;
                    rightClick(I::columnLeft(3,4)+I::columnWidth(4)*.5, I::row(16));
                    captureNeonPage(@"sample-neon.layer-context.pdf"); clickAt(700,24); // Dismiss.
                    undo(); ok = savedState().bytes == before.bytes && ok;
                    redo(); ok = savedState().bytes == pasted.bytes && ok;
                    layerAction(0,1); // Populated destination must confirm before replacing.
                    ok = savedState().bytes == pasted.bytes && ok;
                    clickAt(700,65); // CANCEL, second row of confirmation.
                    ok = savedState().bytes == pasted.bytes && ok;
                    layerAction(0,1); clickAt(700,83); // PASTE / REPLACE.
                    const auto replaced = savedState();
                    ok = replaced.bytes != pasted.bytes && ok;
                    undo(); ok = savedState().bytes == pasted.bytes && ok;
                    layerAction(31,2); // Clear selected layer; leave other numbers intact.
                    const auto cleared = savedState();
                    ok = cleared.bytes != pasted.bytes && ok;
                    undo(); ok = savedState().bytes == pasted.bytes && ok;
                    redo(); ok = savedState().bytes == cleared.bytes && ok;
                    // Cell clear includes the entire sparse stack, not just the empty edit layer.
                    chooseContext(M::padX(0)+M::padWidth*.5,M::padY(0)+M::padHeight*.5,0);
                    chooseContext(M::padX(0)+M::padWidth*.5,M::padY(0)+M::padHeight*.5,2);
                    const auto empty = savedState();
                    ok = empty.bytes.size() < cleared.bytes.size() && ok;
                    undo(); ok = savedState().bytes == cleared.bytes && ok;
                    // Layer keyboard focus also works on an empty grid position;
                    // it must not fall through to the whole-cell clipboard.
                    const auto key = [&](NSString* text) {
                        [document keyDown:[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                            modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:0 context:nil
                            characters:text charactersIgnoringModifiers:text isARepeat:NO keyCode:[text isEqualToString:@"c"] ? 8 : 9]];
                        params->flush(plugin,nullptr,nullptr); plugin->on_main_thread(plugin); redraw();
                    };
                    clickAt(I::columnLeft(0,4)+I::columnWidth(4)*.5,I::row(9)); key(@"c");
                    const auto keySource = savedState();
                    clickAt(I::columnLeft(3,4)+I::columnWidth(4)*.5,I::row(16)); key(@"v");
                    ok = savedState().bytes.size() > keySource.bytes.size() && ok;
                    undo(); ok = savedState().bytes == keySource.bytes && ok;
                    // The miniature STACK surface uses the same layer clipboard/menu.
                    clickAt(M::pageCenter(2),M::pageCenterY); clickAt(M::pageCenter(0),M::banks);
                    chooseContext(M::padX(0)+M::padWidth*.5,M::padY(0)+M::padHeight*.5,0);
                    chooseContext(M::padX(2)+M::padWidth*.5,M::padY(2)+M::padHeight*.5,1);
                    ok = savedState().bytes.size() > keySource.bytes.size() && ok;
                    if (ok) {
                        failureStage = "Sample Neon fit-all stack lanes at 9, 16 and 32 layers";
                        selectPlay(true); inspectorChoice(0,0); layerAction(0,0);
                        unsigned count=3;
                        std::vector<uint8_t> previousRows;
                        for (unsigned total : {9u,16u,32u}) {
                            while (count<total) layerAction(count++,1);
                            const auto sound=savedState();
                            clickAt(416,W::viewCenter); clickAt(416,123); // STACK LANES.
                            const auto rows=waveformPixels();
                            ok=!rows.empty() && rows!=previousRows && savedState().bytes==sound.bytes && ok;
                            previousRows=rows;
                            captureNeonPage(total==9 ? @"sample-neon.all-09-lanes.pdf"
                                : total==16 ? @"sample-neon.all-16-lanes.pdf" : @"sample-neon.all-32-lanes.pdf");
                            // Row labels/geometry do not move with edit-layer selection.
                            const auto labels=regionPixels(W::left,W::top,115,W::height());
                            clickAt(I::columnLeft(0,4)+I::columnWidth(4)*.5,I::row(9));
                            ok=labels==regionPixels(W::left,W::top,115,W::height()) && ok;
                        }
                        if (!ok) std::cerr << "Fit-all stack lane rendering/view-state regression\n";
                    }
                    [parent removeFromSuperview]; [contextWindow close]; [contextWindow release];
                    if (!ok) std::cerr << "Pad/layer copy, paste, confirmation, clear or undo failed\n";
                }
