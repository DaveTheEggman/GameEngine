<screen mode="modal" transition="fade" default-focus="play-btn">

  <Panel style="background: rgba(10, 16, 28, 0.35);">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Flex direction="vertical" align="center" spacing="6">
      <Label id="title" font-family="Lilita One" text="PaperKid" font-size="88" style="text-color: rgb(255, 214, 102);"/>
      <Label id="tagline" text="Deliver the papers before the clock runs out" font-size="24" style="text-color: rgb(225, 230, 240);"/>
    </Flex>

    <Spacer spacer-height="48"/>

    <Flex direction="vertical" spacing="12" width="320">
      <Button id="play-btn" text="New game" height="58" font-size="26" class="primary"/>
      <Button id="settings-btn" text="Settings" height="52" font-size="24"/>
      <Button id="quit-btn" text="Quit" height="52" font-size="24"/>
    </Flex>

    <Spacer spacer-height="36"/>
    <Label id="hint" text="Arrows or stick to choose, Enter or A to pick" font-size="18" style="text-color: rgb(200, 206, 220);"/>

  </Flex>
  </Panel>

</screen>
