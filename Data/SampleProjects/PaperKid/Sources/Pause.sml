<screen mode="modal" transition="fade" default-focus="resume-btn">

  <Panel style="background: rgba(8, 10, 18, 0.55);">
  <Flex direction="vertical" justify="center" align="center" padding="32">

    <Panel padding="40"
           style="background: rounded-rect(rgb(24, 26, 36), radius=14, border-width=2, border=rgb(90, 104, 130));">
      <Flex direction="vertical" align="center" spacing="8">
        <Label id="pause-title" font-family="Lilita One" text="Paused" font-size="56" style="text-color: rgb(225, 230, 240);"/>
        <Spacer spacer-height="18"/>
        <Flex direction="vertical" spacing="10" width="300">
          <Button id="resume-btn" text="Resume" height="52" class="primary" font-size="24"/>
          <Button id="restart-btn" text="Restart block" height="52" font-size="24"/>
          <Button id="settings-btn" text="Settings" height="52" font-size="24"/>
          <Button id="menu-btn" text="Quit to menu" height="52" font-size="24"/>
        </Flex>
      </Flex>
    </Panel>

  </Flex>
  </Panel>

</screen>
