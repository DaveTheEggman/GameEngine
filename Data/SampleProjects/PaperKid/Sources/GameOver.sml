<screen mode="modal" transition="fade" default-focus="again-btn">

  <Panel style="background: rgba(8, 10, 18, 0.7);">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Panel padding="40"
           style="background: rounded-rect(rgb(24, 26, 36), radius=14, border-width=2, border=rgb(255, 214, 102));">
      <Flex direction="vertical" align="center" spacing="8">
        <Label id="over-title" font-family="Lilita One" text="Game over" font-size="56" style="text-color: rgb(255, 214, 102);"/>
        <Label id="over-score" text="" font-size="30" style="text-color: rgb(235, 238, 245);"/>
        <Label id="over-reached" text="" font-size="22" style="text-color: rgb(190, 198, 214);"/>
        <Spacer spacer-height="24"/>
        <Flex direction="vertical" spacing="10" width="300">
          <Button id="again-btn" text="Play again" height="52" class="primary" font-size="24"/>
          <Button id="menu-btn" text="Main menu" height="52" font-size="24"/>
        </Flex>
      </Flex>
    </Panel>

  </Flex>
  </Panel>

</screen>
